/* Server traffic runs inline in MatchmakingTick on the loader thread, which already polls every 100 ms. Title threads only touch the locked units and the pause flag. */
#include "MatchmakingClient.h"
#include "AvatarCache.h"
#include "Connection.h"
#include "Protocol.h"
#include "RelayQueues.h"
#include "Roster.h"
#include "../platform/Log.h"

#define MM_HEARTBEAT_INTERVAL_MS 15000
#define MM_RETRY_INTERVAL_MS 2000
/* Deployment mistake: retry slowly. */
#define MM_REJECTED_RETRY_MS 60000
#define MM_AVATAR_RETRY_MS 2000
#define MM_MAX_FRAME 1536
#define MM_RELAY_FRAME (MATCHMAKING_RELAY_MAX_BYTES + 256)

/* Loader thread only, except atomic g_MmPaused and g_MmJoined. */
static BOOL g_MmActive = FALSE;
static BOOL g_MmJoined = FALSE;
/* Players sent with the last JOIN; the first identifies the console. */
static MatchmakingLocalPlayer g_MmPlayers[MATCHMAKING_MAX_LOCAL_PLAYERS];
static DWORD g_MmPlayerCount = 0;
static XUID g_MmXuid = 0;
/* Last rejection logged; repeats log once. */
static BYTE g_MmLoggedRejectReason = 0;
static BYTE g_MmLoggedRejectVersion = 0;
static DWORD g_MmNextAttemptTick = 0;
static DWORD g_MmNextHeartbeatTick = 0;
static DWORD g_MmNextRosterTick = 0;
/* Per player: avatar stored since the last JOIN. */
static BOOL g_MmAvatarStored[MATCHMAKING_MAX_LOCAL_PLAYERS];
static DWORD g_MmNextAvatarTick = 0;
static volatile LONG g_MmPaused = 0;
/* Users 1-3 in the game session. */
static volatile LONG g_MmPlayersInGame = 0;
static DWORD g_MmNextPlayersCheckTick = 0;
#define MM_PLAYERS_CHECK_MS 1000

/* Remote peers of this group; drop avatars of peers that left. */
static VOID ApplyRoster(const BYTE *payload, DWORD payloadLen)
{
	MatchmakingPeer next[MATCHMAKING_MAX_REMOTE_PEERS];
	DWORD count = 0, groupId = 0;
	ZeroMemory(next, sizeof(next));
	if (!MmDecodeRoster(
			payload, payloadLen, g_MmXuid, next, MATCHMAKING_MAX_REMOTE_PEERS, &count, &groupId)) {
		LOG_WARN("mm", "Matchmaking: roster malformed, ignored");
		return;
	}
	Roster_Set(next, count);
	AvatarCache_ForgetAbsentPeers(next, count);
	LOG_INFO("mm", "Matchmaking: group %u, remote roster peers=%u", groupId, count);
}

/* The server turned a request down: logged once per reason and version, and the next join waits
 * MM_REJECTED_RETRY_MS. */
static VOID NoteRejection(BYTE reason, BYTE serverVersion)
{
	g_MmNextAttemptTick = GetTickCount() + MM_REJECTED_RETRY_MS;
	if (reason == g_MmLoggedRejectReason && serverVersion == g_MmLoggedRejectVersion)
		return;
	g_MmLoggedRejectReason = reason;
	g_MmLoggedRejectVersion = serverVersion;
	if (reason == MM_REJECT_VERSION)
		LOG_ERROR("mm",
			"Matchmaking: server speaks protocol %u, this loader %u: update the loader or the "
			"server",
			serverVersion, (DWORD)MM_PROTOCOL_VERSION);
	else if (reason == MM_REJECT_MALFORMED_JOIN)
		LOG_ERROR("mm", "Matchmaking: server rejected the JOIN as malformed");
	else if (reason == MM_REJECT_UNKNOWN_TYPE)
		LOG_ERROR("mm", "Matchmaking: server rejected a message type as unknown");
	else
		LOG_ERROR("mm", "Matchmaking: server rejected a request (reason %u)", reason);
	LOG_INFO("mm", "Matchmaking: retrying in %u ms", (DWORD)MM_REJECTED_RETRY_MS);
}

/* One request with its response; FALSE on any failure. */
static BOOL Request(const MmWriter *w, BYTE requestType, BYTE responseType, BYTE *response,
	DWORD responseCap, DWORD *responseLen)
{
	if (w->overflow) {
		LOG_WARN("mm", "Matchmaking: request %02X too large for its buffer, dropped", requestType);
		return FALSE;
	}
	if (!Connection_Request(requestType, responseType, w->start, MmWriter_Length(w), response,
			responseCap, responseLen, ApplyRoster)) {
		BYTE reason, serverVersion;
		if (Connection_TakeRejection(&reason, &serverVersion))
			NoteRejection(reason, serverVersion);
		return FALSE;
	}
	return TRUE;
}

static VOID ForgetStoredAvatars()
{
	ZeroMemory(g_MmAvatarStored, sizeof(g_MmAvatarStored));
}

static VOID CloseMmSocket()
{
	Connection_Close();
	g_MmJoined = FALSE;
	ForgetStoredAvatars();
}

static BOOL RefreshRoster()
{
	BYTE payload[MM_MAX_FRAME];
	DWORD payloadLen = 0;
	MmWriter w;

	MmWriter_Init(&w, payload, sizeof(payload)); /* empty payload */
	if (!Request(&w, MM_TYPE_ROSTER, MM_TYPE_ROSTER_REPLY, payload, sizeof(payload), &payloadLen)) {
		LOG_INFO("mm", "Matchmaking: roster refresh failed, will retry joining");
		CloseMmSocket();
		return FALSE;
	}
	ApplyRoster(payload, payloadLen);
	return TRUE;
}

/* Signed-in users 0-3, user 0 first; 0 when user 0 has none. Users 1-3 only while in the game session. */
static DWORD GatherLocalPlayers(MatchmakingLocalPlayer *out)
{
	DWORD user, count = 0;
	for (user = 0; user < MATCHMAKING_MAX_LOCAL_PLAYERS; ++user) {
		XUID xuid = 0;
		XUSER_SIGNIN_INFO info;
		DWORD i;
		BOOL listed = FALSE;

		if (user != 0 && !(g_MmPlayersInGame & (1 << user)))
			continue;
		if (XamUserGetSigninState(user) == eXamUserSigninState_NotSignedIn ||
			FAILED(XamUserGetXUID(user, 7, &xuid)) || xuid == 0) {
			if (user == 0)
				return 0;
			continue;
		}
		for (i = 0; i < count; ++i)
			if (out[i].xuid == xuid)
				listed = TRUE;
		if (listed)
			continue;
		out[count].xuid = xuid;
		out[count].user = user;
		out[count].gamertag[0] = 0;
		ZeroMemory(&info, sizeof(info));
		if (SUCCEEDED(XamUserGetSigninInfo(user, 0, &info))) {
			i = 0;
			while (info.szUserName[i] && i < sizeof(out[count].gamertag) - 1) {
				out[count].gamertag[i] = info.szUserName[i];
				i++;
			}
			out[count].gamertag[i] = 0;
		}
		if (out[count].gamertag[0] == 0) {
			out[count].gamertag[0] = 'P';
			out[count].gamertag[1] = (CHAR)('1' + user);
			out[count].gamertag[2] = 0;
		}
		count++;
	}
	return count;
}

static VOID AttemptJoin()
{
	BYTE payload[MM_MAX_FRAME];
	DWORD payloadLen;
	DWORD population = 0, groupId = 0;
	MmWriter w;

	LOG_INFO("mm", "Matchmaking: AttemptJoin starting");

	g_MmPlayerCount = GatherLocalPlayers(g_MmPlayers);
	if (g_MmPlayerCount == 0) {
		LOG_INFO("mm", "Matchmaking: user 0 not signed in, not joining");
		return;
	}
	g_MmXuid = g_MmPlayers[0].xuid;
	ForgetStoredAvatars();

	LOG_INFO("mm", "Matchmaking: connecting to join as %s with %u local player(s)",
		g_MmPlayers[0].gamertag, g_MmPlayerCount);
	if (!Connection_Open())
		return;

	MmWriter_Init(&w, payload, sizeof(payload));
	MmEncodeJoin(&w, g_MmPlayers, g_MmPlayerCount);
	if (!Request(&w, MM_TYPE_JOIN, MM_TYPE_JOIN_ACK, payload, sizeof(payload), &payloadLen)) {
		LOG_INFO("mm", "Matchmaking: join failed");
		CloseMmSocket();
		return;
	}

	g_MmJoined = TRUE;
	g_MmLoggedRejectReason = 0;
	if (!MmDecodeJoinAck(payload, payloadLen, &population, &groupId))
		LOG_WARN("mm", "Matchmaking: JOIN_ACK short (%u bytes)", payloadLen);
	RefreshRoster();
	g_MmNextHeartbeatTick = GetTickCount() + MM_HEARTBEAT_INTERVAL_MS;
	g_MmNextRosterTick = GetTickCount() + MM_HEARTBEAT_INTERVAL_MS;
	LOG_INFO("mm",
		"Matchmaking: joined as %s (xuid=%08X%08X) with %u local player(s), group %u, population "
		"now %u",
		g_MmPlayers[0].gamertag, (DWORD)(g_MmXuid >> 32), (DWORD)g_MmXuid, g_MmPlayerCount, groupId,
		population);
}

/* TRUE when the signed-in players changed; the server replaces the console, so it rejoins. */
static BOOL LocalPlayersChanged()
{
	MatchmakingLocalPlayer now[MATCHMAKING_MAX_LOCAL_PLAYERS];
	DWORD count = GatherLocalPlayers(now), i;
	if (count != g_MmPlayerCount)
		return TRUE;
	for (i = 0; i < count; ++i)
		if (now[i].xuid != g_MmPlayers[i].xuid)
			return TRUE;
	return FALSE;
}

/* Upload each local avatar once per join. */
static VOID UploadLocalAvatars()
{
	BYTE blob[AVATAR_METADATA_BYTES];
	BYTE payload[MM_MAX_FRAME];
	DWORD i = 0;

	while (i < g_MmPlayerCount && g_MmJoined) {
		DWORD payloadLen = 0;
		MmWriter w;
		const MatchmakingLocalPlayer &player = g_MmPlayers[i];
		if (g_MmAvatarStored[i] || !AvatarGetLocalMetadata(player.user, blob)) {
			i++;
			continue;
		}
		MmWriter_Init(&w, payload, sizeof(payload));
		MmEncodeAvatarPut(&w, player.xuid, blob);
		if (!Request(&w, MM_TYPE_AVATAR_PUT, MM_TYPE_AVATAR_ACK, payload, sizeof(payload),
				&payloadLen)) {
			LOG_INFO("mm", "Matchmaking: avatar upload failed");
			CloseMmSocket();
			return;
		}
		if (payloadLen >= 1 && payload[0] == 1) {
			g_MmAvatarStored[i] = TRUE;
			LOG_INFO("mm", "Matchmaking: avatar of %s (user %u) uploaded (%u bytes)",
				player.gamertag, player.user, (DWORD)AVATAR_METADATA_BYTES);
		}
		i++;
	}
}

/* Fetch missing peer avatar blobs. */
static VOID FetchPeerAvatars()
{
	MatchmakingPeer peers[MATCHMAKING_MAX_REMOTE_PEERS];
	DWORD n = Roster_Get(peers, MATCHMAKING_MAX_REMOTE_PEERS);
	DWORD i;

	for (i = 0; i < n; ++i) {
		BYTE payload[MM_MAX_FRAME];
		DWORD payloadLen = 0;
		const BYTE *blob;
		MmWriter w;

		if (AvatarCache_HasPeer(peers[i].xuid))
			continue;
		MmWriter_Init(&w, payload, sizeof(payload));
		MmEncodeAvatarGet(&w, peers[i].xuid);
		if (!Request(&w, MM_TYPE_AVATAR_GET, MM_TYPE_AVATAR_REPLY, payload, sizeof(payload),
				&payloadLen)) {
			LOG_INFO("mm", "Matchmaking: avatar fetch failed");
			CloseMmSocket();
			return;
		}
		if (MmDecodeAvatarReply(payload, payloadLen, &blob)) {
			AvatarCache_StorePeer(peers[i].xuid, blob);
			LOG_INFO("mm", "Matchmaking: avatar of %08X%08X cached", (DWORD)(peers[i].xuid >> 32),
				(DWORD)peers[i].xuid);
		}
	}
}

static VOID QueueIncoming(VOID *ctx, ULONGLONG from, const BYTE *data, DWORD len)
{
	(void)ctx;
	RelayQueues_PushIn(from, data, len);
}

/* Sender is a joined local player when it matches; else user 0. */
static ULONGLONG RelaySender(ULONGLONG from)
{
	DWORD i = 0;
	while (i < g_MmPlayerCount) {
		if (from != 0 && g_MmPlayers[i].xuid == from)
			return from;
		i++;
	}
	return g_MmXuid;
}

/* Push queued outgoing payloads; collect incoming ones. */
static VOID PumpRelay()
{
	static RelayOutItem item;
	static BYTE payload[MM_RELAY_FRAME];
	DWORD sent;

	for (sent = 0; sent < 8 && g_MmJoined; ++sent) {
		DWORD payloadLen = 0;
		MmWriter w;

		if (!RelayQueues_PopOut(&item))
			break;
		MmWriter_Init(&w, payload, sizeof(payload));
		MmEncodeRelayPut(
			&w, RelaySender(item.from), item.targets, item.targetCount, item.data, item.len);
		if (!Request(
				&w, MM_TYPE_RELAY_PUT, MM_TYPE_RELAY_ACK, payload, sizeof(payload), &payloadLen)) {
			LOG_INFO("mm", "Matchmaking: relay send failed");
			CloseMmSocket();
			return;
		}
	}

	if (g_MmJoined && Roster_Count() > 0) {
		DWORD payloadLen = 0;
		MmWriter w;

		MmWriter_Init(&w, payload, sizeof(payload));
		MmEncodeXuid(&w, g_MmXuid);
		if (!Request(&w, MM_TYPE_RELAY_POLL, MM_TYPE_RELAY_REPLY, payload, sizeof(payload),
				&payloadLen)) {
			LOG_INFO("mm", "Matchmaking: relay poll failed");
			CloseMmSocket();
			return;
		}
		MmDecodeRelayReply(payload, payloadLen, QueueIncoming, NULL);
	}
}

static VOID SendHeartbeat()
{
	BYTE payload[MM_MAX_FRAME];
	DWORD payloadLen;
	MmWriter w;

	MmWriter_Init(&w, payload, sizeof(payload));
	MmEncodeXuid(&w, g_MmXuid);
	if (!Request(
			&w, MM_TYPE_HEARTBEAT, MM_TYPE_HEARTBEAT_ACK, payload, sizeof(payload), &payloadLen)) {
		LOG_INFO("mm", "Matchmaking: heartbeat failed, will retry joining");
		CloseMmSocket();
		return;
	}
	LOG_INFO("mm", "Matchmaking: heartbeat ok, population now %u",
		MmDecodePopulation(payload, payloadLen));
}

static VOID SendLeave()
{
	BYTE payload[MM_MAX_FRAME];
	MmWriter w;

	MmWriter_Init(&w, payload, sizeof(payload));
	MmEncodeXuid(&w, g_MmXuid);
	Connection_Send(MM_TYPE_LEAVE, payload, MmWriter_Length(&w));
	LOG_INFO("mm", "Matchmaking: left the session");
}


ULONGLONG MatchmakingSelfXuid()
{
	return g_MmXuid;
}

BOOL MatchmakingRelaySend(
	ULONGLONG from, const ULONGLONG *targets, DWORD targetCount, const BYTE *data, DWORD len)
{
	if (!g_MmJoined)
		return FALSE;
	return RelayQueues_PushOut(from, targets, targetCount, data, len);
}

BOOL MatchmakingRelayReceive(ULONGLONG *from, BYTE *out, DWORD capacity, DWORD *len)
{
	return RelayQueues_PopIn(from, out, capacity, len);
}

BOOL MatchmakingGetPeerAvatar(ULONGLONG xuid, BYTE *out)
{
	return AvatarCache_GetPeer(xuid, out);
}

DWORD MatchmakingGetPeers(MatchmakingPeer *out, DWORD capacity)
{
	return Roster_Get(out, capacity);
}

VOID MatchmakingSetPlayersInGame(DWORD userMask)
{
	InterlockedExchange(&g_MmPlayersInGame, (LONG)(userMask & 0xE));
}

VOID MatchmakingPause()
{
	InterlockedExchange(&g_MmPaused, 1);
}

VOID MatchmakingResume()
{
	if (InterlockedExchange(&g_MmPaused, 0))
		g_MmNextAttemptTick = 0;
}

/* Once per loader poll. No-op unless a matchmaking-eligible title runs. */
VOID MatchmakingTick()
{
	DWORD now;
	if (!g_MmActive)
		return;
	if (g_MmPaused) {
		if (g_MmJoined) {
			SendLeave();
			CloseMmSocket();
			Roster_Clear();
			LOG_INFO("mm", "Matchmaking: paused (left the game session)");
		}
		return;
	}
	now = GetTickCount();
	if (!g_MmJoined) {
		if ((LONG)(now - g_MmNextAttemptTick) < 0)
			return;
		g_MmNextAttemptTick = now + MM_RETRY_INTERVAL_MS;
		AttemptJoin();
		return;
	}
	/* A joining or leaving player shows on the other consoles. */
	if ((LONG)(now - g_MmNextPlayersCheckTick) >= 0) {
		g_MmNextPlayersCheckTick = now + MM_PLAYERS_CHECK_MS;
		if (LocalPlayersChanged()) {
			LOG_INFO("mm", "Matchmaking: local players changed, rejoining");
			SendLeave();
			CloseMmSocket();
			Roster_Clear();
			g_MmNextAttemptTick = now;
			return;
		}
	}
	if ((LONG)(now - g_MmNextHeartbeatTick) >= 0) {
		g_MmNextHeartbeatTick = now + MM_HEARTBEAT_INTERVAL_MS;
		SendHeartbeat();
	}
	if (g_MmJoined && (LONG)(now - g_MmNextRosterTick) >= 0) {
		RefreshRoster();
		g_MmNextRosterTick = now + MM_HEARTBEAT_INTERVAL_MS;
	}
	if (g_MmJoined)
		PumpRelay();
	if (g_MmJoined && (LONG)(now - g_MmNextAvatarTick) >= 0) {
		g_MmNextAvatarTick = now + MM_AVATAR_RETRY_MS;
		UploadLocalAvatars();
		if (g_MmJoined)
			FetchPeerAvatars();
	}
}

/* After a socket fault: forget and rejoin after the retry interval. */
VOID MatchmakingAbandonConnection()
{
	Connection_Forget();
	g_MmJoined = FALSE;
	ForgetStoredAvatars();
	g_MmNextAttemptTick = GetTickCount() + MM_RETRY_INTERVAL_MS;
}

VOID SetupMatchmakingClient()
{
	if (g_MmActive)
		return;
	g_MmActive = TRUE;
	InterlockedExchange(&g_MmPaused, 0);
	InterlockedExchange(&g_MmPlayersInGame, 0);
	g_MmNextAttemptTick = 0;
	Roster_Clear();
	AvatarCache_ClearPeers();
	ForgetStoredAvatars();
	RelayQueues_Reset();
	LOG_INFO("mm", "Matchmaking: enabled for this title session");
}

VOID TeardownMatchmakingClient()
{
	if (!g_MmActive)
		return;
	if (g_MmJoined)
		SendLeave();
	CloseMmSocket();
	Roster_Clear();
	g_MmActive = FALSE;
}
