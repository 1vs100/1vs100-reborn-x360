#include "Roster.h"
#include "../platform/SpinLock.h"

static SpinLock g_RosterLock = SPINLOCK_INIT;
static MatchmakingPeer g_Peers[MATCHMAKING_MAX_REMOTE_PEERS];
static DWORD g_PeerCount = 0;

VOID Roster_Set(const MatchmakingPeer *peers, DWORD count)
{
	if (count > MATCHMAKING_MAX_REMOTE_PEERS)
		count = MATCHMAKING_MAX_REMOTE_PEERS;
	SpinLock_Lock(&g_RosterLock);
	ZeroMemory(g_Peers, sizeof(g_Peers));
	memcpy(g_Peers, peers, count * sizeof(*peers));
	g_PeerCount = count;
	SpinLock_Unlock(&g_RosterLock);
}

VOID Roster_Clear()
{
	SpinLock_Lock(&g_RosterLock);
	ZeroMemory(g_Peers, sizeof(g_Peers));
	g_PeerCount = 0;
	SpinLock_Unlock(&g_RosterLock);
}

DWORD Roster_Get(MatchmakingPeer *out, DWORD capacity)
{
	DWORD count;
	if (!out || capacity == 0)
		return 0;
	SpinLock_Lock(&g_RosterLock);
	count = g_PeerCount;
	if (count > capacity)
		count = capacity;
	memcpy(out, g_Peers, count * sizeof(*out));
	SpinLock_Unlock(&g_RosterLock);
	return count;
}

DWORD Roster_Count()
{
	DWORD count;
	SpinLock_Lock(&g_RosterLock);
	count = g_PeerCount;
	SpinLock_Unlock(&g_RosterLock);
	return count;
}
