#include "AvatarCache.h"
#include "../platform/SpinLock.h"
#include "../platform/ImportHook.h"
#include "../platform/Log.h"

#define XAM_ORD_AVATAR_GET_METADATA_RANDOM 1515u
#define XAM_ORD_AVATAR_GET_METADATA_LOCAL_USER 1502u
#define WARMUP_ATTEMPTS 40
#define WARMUP_INTERVAL_MS 250

typedef DWORD(NTAPI *XamAvatarGetMetadataRandom_t)(BYTE, DWORD, PVOID, PVOID);
typedef DWORD(NTAPI *XamAvatarGetMetadataLocalUser_t)(DWORD, PVOID, PVOID);

enum WarmupStep {
	WARMUP_IDLE,
	WARMUP_RANDOM,     /* warms XAM avatar paths */
	WARMUP_LOCAL_USER, /* metadata the relay uploads */
	WARMUP_DONE,
};

static WarmupStep g_Step = WARMUP_IDLE;
static DWORD g_Attempt = 0;
static DWORD g_NextTick = 0;
static BYTE g_RandomAvatarMetadata[AVATAR_METADATA_BYTES];
static BYTE g_LocalUserAvatarMetadata[AVATAR_METADATA_BYTES];
static volatile LONG g_LocalAvatarReady = 0;
static VOID DumpAvatarMetadata(const char *tag, const BYTE *meta, DWORD len);

/* Users 1-3 read on demand after user 0. */
static BYTE g_OtherUserMetadata[MATCHMAKING_MAX_LOCAL_PLAYERS][AVATAR_METADATA_BYTES];
static BOOL g_OtherUserReady[MATCHMAKING_MAX_LOCAL_PLAYERS];

static DWORD ReadLocalUser(DWORD user, BYTE *out)
{
	XamAvatarGetMetadataLocalUser_t getLocalUser = (XamAvatarGetMetadataLocalUser_t)ResolveFunction(
		"xam.xex", XAM_ORD_AVATAR_GET_METADATA_LOCAL_USER);
	if (!getLocalUser)
		return ERROR_PROC_NOT_FOUND;
	return getLocalUser(user, out, NULL);
}

BOOL AvatarGetLocalMetadata(DWORD user, BYTE *out)
{
	if (!g_LocalAvatarReady || user >= MATCHMAKING_MAX_LOCAL_PLAYERS)
		return FALSE;
	if (user == 0) {
		memcpy(out, g_LocalUserAvatarMetadata, sizeof(g_LocalUserAvatarMetadata));
		return TRUE;
	}
	if (!g_OtherUserReady[user]) {
		DWORD hr = ReadLocalUser(user, g_OtherUserMetadata[user]);
		if (hr != 0) {
			LOG_INFO("xam", "XamAvatarGetMetadataLocalUser(%u) hr=%08X", user, hr);
			return FALSE;
		}
		g_OtherUserReady[user] = TRUE;
		DumpAvatarMetadata("local-user-n", g_OtherUserMetadata[user], AVATAR_METADATA_BYTES);
	}
	memcpy(out, g_OtherUserMetadata[user], AVATAR_METADATA_BYTES);
	return TRUE;
}

/* Hex-dump head; say whether it holds anything. */
static VOID DumpAvatarMetadata(const char *tag, const BYTE *meta, DWORD len)
{
	DWORD i, nonZero = 0, sum = 0;
	for (i = 0; i < len; ++i) {
		if (meta[i])
			++nonZero;
		sum = sum * 31 + meta[i];
	}
	LOG_INFO(
		"xam", "avatar metadata [%s]: %u/%u bytes non-zero, hash=%08X", tag, nonZero, len, sum);
	for (i = 0; i < 128; i += 16)
		LOG_INFO("xam",
			"avatar metadata [%s] +%02X: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
			tag, i, meta[i], meta[i + 1], meta[i + 2], meta[i + 3], meta[i + 4], meta[i + 5],
			meta[i + 6], meta[i + 7], meta[i + 8], meta[i + 9], meta[i + 10], meta[i + 11],
			meta[i + 12], meta[i + 13], meta[i + 14], meta[i + 15]);
}

VOID AvatarCache_StartWarmup()
{
	g_Step = WARMUP_RANDOM;
	g_Attempt = 0;
	g_NextTick = GetTickCount();
}

VOID AvatarCache_Reset()
{
	g_Step = WARMUP_IDLE;
	InterlockedExchange(&g_LocalAvatarReady, 0);
	ZeroMemory(g_OtherUserReady, sizeof(g_OtherUserReady));
	ZeroMemory(g_RandomAvatarMetadata, sizeof(g_RandomAvatarMetadata));
	ZeroMemory(g_LocalUserAvatarMetadata, sizeof(g_LocalUserAvatarMetadata));
}

/* One attempt of the current step. */
static DWORD TryStep(const char **api)
{
	if (g_Step == WARMUP_RANDOM) {
		XamAvatarGetMetadataRandom_t getRandom = (XamAvatarGetMetadataRandom_t)ResolveFunction(
			"xam.xex", XAM_ORD_AVATAR_GET_METADATA_RANDOM);
		*api = "XamAvatarGetMetadataRandom";
		if (!getRandom)
			return ERROR_PROC_NOT_FOUND;
		return getRandom(0x03 /* XAVATAR_BODY_TYPE_ALL */, 1, g_RandomAvatarMetadata, NULL);
	} else {
		*api = "XamAvatarGetMetadataLocalUser";
		return ReadLocalUser(0, g_LocalUserAvatarMetadata);
	}
}

VOID AvatarCache_Tick()
{
	const char *api = "";
	DWORD now = GetTickCount();
	DWORD hr;

	if (g_Step != WARMUP_RANDOM && g_Step != WARMUP_LOCAL_USER)
		return;
	if ((LONG)(now - g_NextTick) < 0)
		return;
	g_NextTick = now + WARMUP_INTERVAL_MS;

	hr = TryStep(&api);
	if (hr == ERROR_PROC_NOT_FOUND) {
		LOG_INFO("xam", "avatar warm-up: %s not resolved, stopping", api);
		g_Step = WARMUP_DONE;
		return;
	}
	if (hr != 0) {
		if (g_Attempt == 0 || g_Attempt == WARMUP_ATTEMPTS - 1)
			LOG_INFO("xam", "%s attempt=%u hr=%08X", api, g_Attempt, hr);
		if (++g_Attempt >= WARMUP_ATTEMPTS) {
			LOG_INFO("xam", "%s gave up after %u attempts hr=%08X", api, g_Attempt, hr);
			g_Step = WARMUP_DONE;
		}
		return;
	}
	LOG_INFO("xam", "%s succeeded after %u attempts", api, g_Attempt);
	g_Attempt = 0;
	if (g_Step == WARMUP_RANDOM) {
		DumpAvatarMetadata("random", g_RandomAvatarMetadata, sizeof(g_RandomAvatarMetadata));
		g_Step = WARMUP_LOCAL_USER;
		return;
	}
	DumpAvatarMetadata(
		"local-user-0", g_LocalUserAvatarMetadata, sizeof(g_LocalUserAvatarMetadata));
	InterlockedExchange(&g_LocalAvatarReady, 1);
	g_Step = WARMUP_DONE;
}


struct PeerAvatar {
	ULONGLONG xuid;
	BOOL valid;
	BYTE data[AVATAR_METADATA_BYTES];
};
static PeerAvatar g_PeerAvatars[MATCHMAKING_MAX_REMOTE_PEERS];
static SpinLock g_PeerAvatarLock = SPINLOCK_INIT;

BOOL AvatarCache_HasPeer(ULONGLONG xuid)
{
	BOOL have = FALSE;
	DWORD a;
	SpinLock_Lock(&g_PeerAvatarLock);
	for (a = 0; a < MATCHMAKING_MAX_REMOTE_PEERS; ++a)
		if (g_PeerAvatars[a].valid && g_PeerAvatars[a].xuid == xuid)
			have = TRUE;
	SpinLock_Unlock(&g_PeerAvatarLock);
	return have;
}

VOID AvatarCache_StorePeer(ULONGLONG xuid, const BYTE *blob)
{
	DWORD slot = MATCHMAKING_MAX_REMOTE_PEERS, a;
	SpinLock_Lock(&g_PeerAvatarLock);
	for (a = 0; a < MATCHMAKING_MAX_REMOTE_PEERS; ++a)
		if (g_PeerAvatars[a].xuid == xuid) {
			slot = a;
			break;
		}
	for (a = 0; slot == MATCHMAKING_MAX_REMOTE_PEERS && a < MATCHMAKING_MAX_REMOTE_PEERS; ++a)
		if (!g_PeerAvatars[a].valid) {
			slot = a;
			break;
		}
	if (slot < MATCHMAKING_MAX_REMOTE_PEERS) {
		g_PeerAvatars[slot].xuid = xuid;
		memcpy(g_PeerAvatars[slot].data, blob, AVATAR_METADATA_BYTES);
		g_PeerAvatars[slot].valid = TRUE;
	}
	SpinLock_Unlock(&g_PeerAvatarLock);
}

BOOL AvatarCache_GetPeer(ULONGLONG xuid, BYTE *out)
{
	BOOL found = FALSE;
	DWORD a;
	SpinLock_Lock(&g_PeerAvatarLock);
	for (a = 0; a < MATCHMAKING_MAX_REMOTE_PEERS; ++a)
		if (g_PeerAvatars[a].valid && g_PeerAvatars[a].xuid == xuid) {
			memcpy(out, g_PeerAvatars[a].data, AVATAR_METADATA_BYTES);
			found = TRUE;
			break;
		}
	SpinLock_Unlock(&g_PeerAvatarLock);
	return found;
}

VOID AvatarCache_ForgetAbsentPeers(const MatchmakingPeer *peers, DWORD count)
{
	DWORD a, b;
	SpinLock_Lock(&g_PeerAvatarLock);
	for (a = 0; a < MATCHMAKING_MAX_REMOTE_PEERS; ++a) {
		BOOL present = FALSE;
		for (b = 0; b < count; ++b)
			if (peers[b].xuid == g_PeerAvatars[a].xuid)
				present = TRUE;
		if (!present)
			ZeroMemory(&g_PeerAvatars[a], sizeof(g_PeerAvatars[a]));
	}
	SpinLock_Unlock(&g_PeerAvatarLock);
}

VOID AvatarCache_ClearPeers()
{
	SpinLock_Lock(&g_PeerAvatarLock);
	ZeroMemory(g_PeerAvatars, sizeof(g_PeerAvatars));
	SpinLock_Unlock(&g_PeerAvatarLock);
}
