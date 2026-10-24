/* QNet sends relay through the matchmaking server; payloads deliver on the title main thread. */
#include "PatchTables.h"
#include "../registry/PatchHelpers.h"
#include "../matchmaking/MatchmakingClient.h"
#include "NativeRemoteMembers.h"
#include "../platform/Log.h"
#include "../platform/MainLoopWatchdog.h"
#include "../platform/Text.h"
#include "../title/TitleLayout.h"

/* Payloads arrive on the loader thread; the event must post on the main thread, outside any Lua call. */
#define TITLE_NET_EVENT_DATA 18u
typedef VOID(__fastcall *MainPump_t)(DWORD self);
typedef VOID(__fastcall *PostNetEvent_t)(
	DWORD queue, DWORD type, const CHAR *from, const CHAR *to, DWORD len, const BYTE *data);
static MainPump_t g_MainPumpOriginal = NULL;

/* Low half of the title load word. */
static DWORD EventQueueOffset()
{
	return *(const DWORD *)TitleSiteAddress(SITE_EVENT_QUEUE_LOAD) & 0xFFFFu;
}

static volatile LONG g_RelayDelivered = 0;
static volatile LONG g_RelayRejected = 0;
static VOID DeliverRelayedPayloads(DWORD mainThread)
{
	static BYTE data[MATCHMAKING_RELAY_MAX_BYTES];
	ULONGLONG from;
	DWORD len, queue, budget, offset = EventQueueOffset();
	CHAR fromStr[24], toStr[24];

	for (budget = 0; budget < 8; ++budget) {
		if (!MatchmakingRelayReceive(&from, data, sizeof(data), &len))
			return;
		queue = (IsTitleHeapPtr(mainThread, 4) &&
					*(DWORD *)mainThread == TitleSiteAddress(SITE_MAIN_THREAD_VTABLE))
			? *(DWORD *)(mainThread + offset)
			: 0;
		if (!IsTitleHeapPtr(queue, 1)) {
			if (Log_Every(&g_RelayRejected, 32))
				LOG_INFO("qnet",
					"relay deliver: main-thread object %08X queue %08X (+%X) rejected (#%u)",
					mainThread, queue, offset, g_RelayRejected);
			continue;
		}
		Text_FormatDecimalU64(from, fromStr);
		Text_FormatDecimalU64(MatchmakingSelfXuid(), toStr);
		if (Log_Every(&g_RelayDelivered, 32))
			LOG_INFO("qnet", "relay deliver #%u: from %s len=%u", g_RelayDelivered, fromStr, len);
		((PostNetEvent_t)TitleSiteAddress(SITE_POST_NET_EVENT))(
			queue, TITLE_NET_EVENT_DATA, fromStr, toStr, len, data);
	}
}

static VOID __fastcall MainPumpHook(DWORD self)
{
	MainLoopWatchdog_Pump();
	DeliverRelayedPayloads(self);
	if (g_MainPumpOriginal)
		g_MainPumpOriginal(self);
}

/* No XAM data plane liveless: relay type-0 payloads and report done. */
/* r4 is the full 64-bit sender XUID. */
typedef DWORD(__fastcall *QNetSendData_t)(DWORD self, ULONGLONG from, DWORD b, DWORD c, DWORD d);
static QNetSendData_t g_QNetSendDataOriginal = NULL;
static volatile LONG g_QNetSendDropped = 0;
/* Block: +8 length, +0xC buffer {type, BE length, payload}. Type 0 is player events; r5 targets, empty is everyone. */
static volatile LONG g_QNetRelayed = 0;
static VOID RelayQNetSend(ULONGLONG from, DWORD targetList, DWORD block)
{
	ULONGLONG targets[MATCHMAKING_RELAY_MAX_TARGETS];
	DWORD targetCount = 0, buf, bufLen, dataLen;
	if (!IsTitleDataPtr(block, 4))
		return;
	bufLen = *(DWORD *)(block + QNetSendBlock::kBufferLen);
	buf = *(DWORD *)(block + QNetSendBlock::kBuffer);
	if (!IsTitleDataPtr(buf, 1) || bufLen < 4)
		return;
	if (*(BYTE *)buf != 0)
		return;
	dataLen = ((DWORD)((BYTE *)buf)[1] << 8) | ((BYTE *)buf)[2];
	if (dataLen == 0 || dataLen > MATCHMAKING_RELAY_MAX_BYTES) {
		LOG_INFO("qnet", "QNet relay skipped: bufLen=%u dataLen=%u", bufLen, dataLen);
		return;
	}
	if (IsTitleDataPtr(targetList, 8)) {
		while (targetCount < MATCHMAKING_RELAY_MAX_TARGETS &&
			((ULONGLONG *)targetList)[targetCount] != 0) {
			targets[targetCount] = ((ULONGLONG *)targetList)[targetCount];
			++targetCount;
		}
	}
	if (Log_Every(&g_QNetRelayed, 32))
		LOG_INFO("qnet",
			"QNet relay #%u: from %08X%08X bufLen=%u dataLen=%u targets=%u first=%02X%02X%02X%02X",
			g_QNetRelayed, (DWORD)(from >> 32), (DWORD)from, bufLen, dataLen, targetCount,
			((BYTE *)buf)[3], ((BYTE *)buf)[4], ((BYTE *)buf)[5], ((BYTE *)buf)[6]);
	MatchmakingRelaySend(from, targets, targetCount, (const BYTE *)(buf + 3), dataLen);
}

static DWORD __fastcall QNetSendDataHook(DWORD self, ULONGLONG from, DWORD b, DWORD c, DWORD d)
{
	MatchmakingPeer peers[MATCHMAKING_MAX_REMOTE_PEERS];
	/* Also while a member waits out its removal grace. */
	if (MatchmakingGetPeers(peers, MATCHMAKING_MAX_REMOTE_PEERS) > 0 ||
		NativeRemoteMembersPresent()) {
		RelayQNetSend(from, b, c);
		if (Log_Every(&g_QNetSendDropped, 16))
			LOG_INFO("qnet", "QNet send dropped (#%u): no XAM data plane", g_QNetSendDropped);
		return 1;
	}
	return g_QNetSendDataOriginal ? g_QNetSendDataOriginal(self, from, b, c, d) : 0;
}

/* QNet avatar fetch is dropped above: feed the relayed blob after the original marks the request sent. */
typedef VOID(__fastcall *RemoteRequestAvatar_t)(DWORD manager, ULONGLONG fromXuid, DWORD entry);
typedef VOID(__fastcall *HandleAvatarData_t)(
	DWORD manager, ULONGLONG xuid, DWORD success, DWORD count, DWORD data);
static RemoteRequestAvatar_t g_RemoteRequestAvatarOriginal = NULL;
static volatile LONG g_RemoteAvatarMisses = 0;
static VOID __fastcall RemoteRequestAvatarHook(DWORD manager, ULONGLONG fromXuid, DWORD entry)
{
	ULONGLONG wanted =
		entry >= kTitleHeapBase ? *(ULONGLONG *)(entry + AvatarRequestEntry::kXuid) : 0;
	/* Static blob: assumes one calling thread. */
	static BYTE blob[AVATAR_METADATA_BYTES];
	if (g_RemoteRequestAvatarOriginal)
		g_RemoteRequestAvatarOriginal(manager, fromXuid, entry);
	if (wanted == 0)
		return;
	if (MatchmakingGetPeerAvatar(wanted, blob)) {
		LOG_INFO("qnet", "avatar sync: feeding relayed metadata for %08X%08X",
			(DWORD)(wanted >> 32), (DWORD)wanted);
		((HandleAvatarData_t)TitleSiteAddress(SITE_AVATAR_HANDLE_DATA))(
			manager, wanted, 1, AVATAR_METADATA_BYTES, (DWORD)blob);
	} else if (Log_Every(&g_RemoteAvatarMisses, 4)) {
		LOG_INFO("qnet", "avatar sync: no relayed metadata yet for %08X%08X (#%u)",
			(DWORD)(wanted >> 32), (DWORD)wanted, g_RemoteAvatarMisses);
	}
}

static VOID InstallSendDataDrop(const PatchDescriptor *self)
{
	HOOK_SITE(SITE_QNET_SEND_DATA, QNetSendDataHook, g_QNetSendDataOriginal, self->id);
}

static VOID InstallMainPumpDelivery(const PatchDescriptor *self)
{
	static const SiteId kCalls[] = {
		SITE_MAIN_THREAD_VTABLE, SITE_POST_NET_EVENT, SITE_EVENT_QUEUE_LOAD};
	if (REQUIRE_SITES(kCalls, self->id))
		HOOK_SITE(SITE_MAIN_PUMP, MainPumpHook, g_MainPumpOriginal, self->id);
}

static VOID InstallAvatarRelay(const PatchDescriptor *self)
{
	static const SiteId kCalls[] = {SITE_AVATAR_HANDLE_DATA};
	if (REQUIRE_SITES(kCalls, self->id))
		HOOK_SITE(SITE_AVATAR_REMOTE_REQUEST, RemoteRequestAvatarHook,
			g_RemoteRequestAvatarOriginal, self->id);
}

static const PatchDescriptor kRows[] = {
	{"QNet send data drop", "qnet_send_relay", ON_TITLE,
		"With a remote peer present, QNet sends go through the matchmaking relay, not XAM",
		InstallSendDataDrop},
	{"main-thread pump (QNet relay delivery)", "qnet_relay_delivery", ON_TITLE,
		"Relayed payloads are posted as network events on the title's main thread",
		InstallMainPumpDelivery},
	{"avatar relay", "avatar_relay", ON_TITLE,
		"Remote avatar requests are answered with metadata relayed by the matchmaking server",
		InstallAvatarRelay},
};
const PatchTable kQNetRelayPatches = PATCH_TABLE(kRows);
