#include "RelayQueues.h"
#include "../platform/SpinLock.h"

#define RELAY_QUEUE 16

struct RelayInItem {
	ULONGLONG from;
	DWORD len;
	BYTE data[MATCHMAKING_RELAY_MAX_BYTES];
};

static SpinLock g_RelayLock = SPINLOCK_INIT;
static RelayOutItem g_Out[RELAY_QUEUE];
static RelayInItem g_In[RELAY_QUEUE];
static DWORD g_OutHead = 0, g_OutTail = 0;
static DWORD g_InHead = 0, g_InTail = 0;

BOOL RelayQueues_PushOut(
	ULONGLONG from, const ULONGLONG *targets, DWORD targetCount, const BYTE *data, DWORD len)
{
	BOOL queued = FALSE;
	DWORD i;
	if (len == 0 || len > MATCHMAKING_RELAY_MAX_BYTES ||
		targetCount > MATCHMAKING_RELAY_MAX_TARGETS)
		return FALSE;
	SpinLock_Lock(&g_RelayLock);
	if (g_OutHead - g_OutTail < RELAY_QUEUE) {
		RelayOutItem *slot = &g_Out[g_OutHead % RELAY_QUEUE];
		slot->from = from;
		slot->targetCount = targetCount;
		for (i = 0; i < targetCount; ++i)
			slot->targets[i] = targets[i];
		slot->len = len;
		memcpy(slot->data, data, len);
		++g_OutHead;
		queued = TRUE;
	}
	SpinLock_Unlock(&g_RelayLock);
	return queued;
}

BOOL RelayQueues_PopOut(RelayOutItem *item)
{
	BOOL got = FALSE;
	SpinLock_Lock(&g_RelayLock);
	if (g_OutHead != g_OutTail) {
		memcpy(item, &g_Out[g_OutTail % RELAY_QUEUE], sizeof(*item));
		++g_OutTail;
		got = TRUE;
	}
	SpinLock_Unlock(&g_RelayLock);
	return got;
}

VOID RelayQueues_PushIn(ULONGLONG from, const BYTE *data, DWORD len)
{
	if (len > MATCHMAKING_RELAY_MAX_BYTES)
		return;
	SpinLock_Lock(&g_RelayLock);
	if (g_InHead - g_InTail < RELAY_QUEUE) {
		RelayInItem *slot = &g_In[g_InHead % RELAY_QUEUE];
		slot->from = from;
		slot->len = len;
		memcpy(slot->data, data, len);
		++g_InHead;
	}
	SpinLock_Unlock(&g_RelayLock);
}

BOOL RelayQueues_PopIn(ULONGLONG *from, BYTE *out, DWORD capacity, DWORD *len)
{
	BOOL got = FALSE;
	SpinLock_Lock(&g_RelayLock);
	if (g_InHead != g_InTail) {
		RelayInItem *slot = &g_In[g_InTail % RELAY_QUEUE];
		if (slot->len <= capacity) {
			*from = slot->from;
			*len = slot->len;
			memcpy(out, slot->data, slot->len);
			got = TRUE;
		}
		++g_InTail;
	}
	SpinLock_Unlock(&g_RelayLock);
	return got;
}

VOID RelayQueues_Reset()
{
	SpinLock_Lock(&g_RelayLock);
	g_OutHead = g_OutTail = g_InHead = g_InTail = 0;
	SpinLock_Unlock(&g_RelayLock);
}
