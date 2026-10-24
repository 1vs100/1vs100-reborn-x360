/* Roster peers join the title session engine as native remote members. */
#include "NativeRemoteMembers.h"
#include "PatchTables.h"
#include "../registry/PatchHelpers.h"
#include "../matchmaking/MatchmakingClient.h"
#include "../platform/Log.h"
#include "../platform/Text.h"
#include "../title/TitleLayout.h"

/* Replay of the remote join without XAM: factory, id, link, update, recompute, valid, notify. Runs on the session thread. */
typedef DWORD(__fastcall *SessionUpdate_t)(DWORD session);
typedef DWORD(__fastcall *SessionMemberNew_t)(DWORD session, ULONGLONG xuid, DWORD index,
	const CHAR *gamertag, DWORD isLocal, DWORD f8, DWORD f9, DWORD f10, DWORD a54, DWORD a5c);
typedef VOID(__fastcall *SessionMemberLink_t)(DWORD session, DWORD member);
typedef int(__fastcall *SessionMemberUpd_t)(DWORD session, DWORD member, DWORD flag);
typedef int(__fastcall *SessionRecompute_t)(DWORD session);
typedef int(__fastcall *SessionIdAssign_t)(DWORD table, DWORD member, DWORD id);
typedef DWORD(__fastcall *SessionFind_t)(DWORD session, ULONGLONG xuid);
static SessionUpdate_t g_SessionUpdateOriginal = NULL;
static DWORD g_NativeJoinLastTick = 0;
static DWORD g_NativeJoinAttempts = 0;

static BOOL SessionReadable(DWORD session)
{
	/* Title heap. */
	return TitleSiteAddress(SITE_SESSION_VTABLE) && IsTitleHeapPtr(session, 1) &&
		session < kSessionHeapEnd && *(DWORD *)session == TitleSiteAddress(SITE_SESSION_VTABLE);
}

/* Index 0: 1-3 would alias a local member and drop the host on sign-in. */
#define REMOTE_MEMBER_INDEX 0

static VOID NativeAddRemoteMember(DWORD session, const MatchmakingPeer &peer)
{
	CHAR name[16];
	DWORD member;
	int hr;
	SessionMemberNew_t create = (SessionMemberNew_t)TitleSiteAddress(SITE_SESSION_MEMBER_NEW);
	SessionIdAssign_t assign = (SessionIdAssign_t)TitleSiteAddress(SITE_SESSION_ID_ASSIGN);
	SessionMemberLink_t link = (SessionMemberLink_t)TitleSiteAddress(SITE_SESSION_MEMBER_LINK);
	SessionMemberUpd_t upd = (SessionMemberUpd_t)TitleSiteAddress(SITE_SESSION_MEMBER_UPD);
	SessionRecompute_t recompute = (SessionRecompute_t)TitleSiteAddress(SITE_SESSION_RECOMPUTE);
	DWORD listener = *(DWORD *)(session + Session::kListener);

	Text_AppendWide(name, 0, sizeof(name), peer.gamertag, 15);

	member = create(session, peer.xuid, REMOTE_MEMBER_INDEX, name, 0, 0, 0, 0, 0, 0);
	LOG_INFO("qnet", "QNet native join: member new -> %08X (xuid %08X%08X '%s')", member,
		(DWORD)(peer.xuid >> 32), (DWORD)peer.xuid, name);
	if (!member)
		return;
	hr = assign(session + Session::kIdTable, member, 0xFF);
	LOG_INFO("qnet", "QNet native join: id assign hr=%08X id=%02X", (DWORD)hr,
		*(BYTE *)(member + Member::kId));
	if (hr < 0)
		return;
	*(BYTE *)(session + Session::kPublicUsed) += 1;
	link(session, member);
	hr = upd(session, member, 1);
	LOG_INFO("qnet", "QNet native join: link+update hr=%08X", (DWORD)hr);
	if (hr < 0)
		return;
	hr = recompute(session);
	LOG_INFO("qnet", "QNet native join: recompute hr=%08X", (DWORD)hr);
	if (hr < 0)
		return;
	*(DWORD *)(member + Member::kFlags) |= Member::kFlagValid;
	*(DWORD *)(session + Session::kMemberCount) += 1;
	LOG_INFO("qnet", "QNet native join: valid flags=%08X count=%u -> notify listener %08X",
		*(DWORD *)(member + Member::kFlags), *(DWORD *)(session + Session::kMemberCount), listener);
	if (listener) {
		DWORD *lvt = *(DWORD **)listener;
		((VOID(__fastcall *)(DWORD, DWORD))lvt[SessionListenerVtbl::kMemberAdded])(
			listener, member);
	}
	LOG_INFO("qnet", "QNet native join: notified");
}

/* Mirror of the add. */
typedef VOID(__fastcall *SessionIdRemove_t)(DWORD table, DWORD member);
typedef VOID(__fastcall *SessionMemberUnlink_t)(DWORD session, DWORD member, DWORD flag);
typedef VOID(__fastcall *SessionChainUnlink_t)(DWORD member);
typedef VOID(__fastcall *SessionRelease_t)(DWORD member);
static ULONGLONG g_NativeJoined[MATCHMAKING_MAX_REMOTE_PEERS];
/* When first missing; 0 while listed. */
static DWORD g_NativeMissingSince[MATCHMAKING_MAX_REMOTE_PEERS];
/* Rejoin briefly leaves the roster; the grace avoids churn. */
#define NATIVE_REAP_GRACE_MS 10000

static VOID NativeRemoveRemoteMember(DWORD session, ULONGLONG xuid)
{
	DWORD *vtbl = *(DWORD **)session;
	DWORD member = ((SessionFind_t)vtbl[Session::kVtblFindMember])(session, xuid);
	DWORD listener = *(DWORD *)(session + Session::kListener);
	if (!member)
		return;
	if (*(DWORD *)(member + Member::kFlags) & Member::kFlagLocal)
		return; /* never a local member */
	LOG_INFO("qnet", "QNet native leave: member %08X xuid %08X%08X", member, (DWORD)(xuid >> 32),
		(DWORD)xuid);
	if (listener) {
		DWORD *lvt = *(DWORD **)listener;
		((VOID(__fastcall *)(DWORD, DWORD))lvt[SessionListenerVtbl::kMemberRemoved])(
			listener, member);
	}
	*(DWORD *)(member + Member::kFlags) &= ~Member::kFlagValid;
	if (*(DWORD *)(session + Session::kMemberCount))
		*(DWORD *)(session + Session::kMemberCount) -= 1;
	if (*(BYTE *)(session + Session::kPublicUsed))
		*(BYTE *)(session + Session::kPublicUsed) -= 1;
	((SessionIdRemove_t)TitleSiteAddress(SITE_SESSION_ID_REMOVE))(
		session + Session::kIdTable, member);
	((SessionMemberUnlink_t)TitleSiteAddress(SITE_SESSION_MEMBER_UNLINK))(session, member, 1);
	((SessionChainUnlink_t)TitleSiteAddress(SITE_SESSION_CHAIN_UNLINK))(member);
	*(DWORD *)(member + Member::kTag) = 0;
	((SessionRelease_t)TitleSiteAddress(SITE_SESSION_RELEASE))(member);
	((SessionRelease_t)TitleSiteAddress(SITE_SESSION_RELEASE))(member);
	LOG_INFO(
		"qnet", "QNet native leave: done, count=%u", *(DWORD *)(session + Session::kMemberCount));
}

BOOL NativeRemoteMembersPresent()
{
	DWORD j = 0;
	while (j < MATCHMAKING_MAX_REMOTE_PEERS) {
		if (g_NativeJoined[j])
			return TRUE;
		j++;
	}
	return FALSE;
}

static VOID NativeReapDeparted(DWORD session, const MatchmakingPeer *peers, DWORD n)
{
	DWORD now = GetTickCount();
	for (DWORD j = 0; j < MATCHMAKING_MAX_REMOTE_PEERS; ++j) {
		BOOL present = FALSE;
		if (!g_NativeJoined[j])
			continue;
		for (DWORD i = 0; i < n; ++i)
			if (peers[i].xuid == g_NativeJoined[j])
				present = TRUE;
		if (present) {
			if (g_NativeMissingSince[j])
				LOG_INFO("qnet", "QNet native member %08X%08X back on the roster, kept",
					(DWORD)(g_NativeJoined[j] >> 32), (DWORD)g_NativeJoined[j]);
			g_NativeMissingSince[j] = 0;
			continue;
		}
		if (!g_NativeMissingSince[j]) {
			/* 0 tick reads as 1. */
			g_NativeMissingSince[j] = now ? now : 1;
			LOG_INFO("qnet", "QNet native member %08X%08X off the roster, removed in %u ms",
				(DWORD)(g_NativeJoined[j] >> 32), (DWORD)g_NativeJoined[j],
				(DWORD)NATIVE_REAP_GRACE_MS);
			continue;
		}
		if (now - g_NativeMissingSince[j] < NATIVE_REAP_GRACE_MS)
			continue;
		NativeRemoveRemoteMember(session, g_NativeJoined[j]);
		g_NativeJoined[j] = 0;
		g_NativeMissingSince[j] = 0;
	}
}

/* Leave orphans native members and fatals the host query: remove them first. Same thread. */
VOID NativeRemoveAllBeforeLeave(DWORD session)
{
	if (!SessionReadable(session))
		return;
	for (DWORD j = 0; j < MATCHMAKING_MAX_REMOTE_PEERS; ++j) {
		if (g_NativeJoined[j]) {
			NativeRemoveRemoteMember(session, g_NativeJoined[j]);
			g_NativeJoined[j] = 0;
		}
		g_NativeMissingSince[j] = 0;
	}
}

/* Valid local members for JOIN; pending ones do not count. */
static VOID PublishPlayersInGame(DWORD session)
{
	static DWORD s_Last = 0xFFFFFFFFu;
	DWORD mask = 0, i = 0;
	while (i < MATCHMAKING_MAX_LOCAL_PLAYERS) {
		DWORD member = *(volatile DWORD *)(session + Session::kLocalSlots + 4 * i);
		if (IsTitleHeapPtr(member, 4)) {
			DWORD flags = *(volatile DWORD *)(member + Member::kFlags);
			if ((flags & Member::kFlagLocal) && (flags & Member::kFlagValid))
				mask |= 1u << i;
		}
		i++;
	}
	/* Every update: leaving resets the copy to none. */
	MatchmakingSetPlayersInGame(mask);
	if (mask != s_Last) {
		s_Last = mask;
		LOG_INFO("qnet", "local players in the game session: mask %X", mask);
	}
}

static DWORD __fastcall SessionUpdateHook(DWORD session)
{
	if (SessionReadable(session)) {
		PublishPlayersInGame(session);
		DWORD now = GetTickCount();
		if (now - g_NativeJoinLastTick >= 2000) {
			DWORD state = *(DWORD *)(session + Session::kState);
			DWORD count = *(DWORD *)(session + Session::kMemberCount);
			g_NativeJoinLastTick = now;
			/* Lobby/game with a local member. */
			if ((state == 3 || state == 6) && count >= 1) {
				MatchmakingPeer peers[MATCHMAKING_MAX_REMOTE_PEERS];
				DWORD n = MatchmakingGetPeers(peers, MATCHMAKING_MAX_REMOTE_PEERS);
				DWORD *vtbl = *(DWORD **)session;
				/* Reap even when full: a left console would stay a ghost. */
				if (*(BYTE *)(session + Session::kPublicUsed) <
					*(BYTE *)(session + Session::kMaxPublic)) {
					for (DWORD i = 0; i < n; ++i) {
						DWORD found =
							((SessionFind_t)vtbl[Session::kVtblFindMember])(session, peers[i].xuid);
						if (found) {
							g_NativeJoinAttempts = 0;
							continue;
						}
						if (++g_NativeJoinAttempts > 8) {
							LOG_INFO("qnet", "QNet native join: attempt budget spent");
							break;
						}
						NativeAddRemoteMember(session, peers[i]);
						{
							BOOL known = FALSE;
							for (DWORD j = 0; j < MATCHMAKING_MAX_REMOTE_PEERS; ++j)
								if (g_NativeJoined[j] == peers[i].xuid)
									known = TRUE;
							for (DWORD j = 0; !known && j < MATCHMAKING_MAX_REMOTE_PEERS; ++j)
								if (!g_NativeJoined[j]) {
									g_NativeJoined[j] = peers[i].xuid;
									break;
								}
						}
						break;
					}
				}
				NativeReapDeparted(session, peers, n);
			}
		}
	}
	return g_SessionUpdateOriginal ? g_SessionUpdateOriginal(session) : 0;
}

static VOID InstallNativeRemoteMembers(const PatchDescriptor *self)
{
	static const SiteId kCalls[] = {SITE_SESSION_VTABLE, SITE_SESSION_MEMBER_NEW,
		SITE_SESSION_ID_ASSIGN, SITE_SESSION_MEMBER_LINK, SITE_SESSION_MEMBER_UPD,
		SITE_SESSION_RECOMPUTE, SITE_SESSION_ID_REMOVE, SITE_SESSION_MEMBER_UNLINK,
		SITE_SESSION_CHAIN_UNLINK, SITE_SESSION_RELEASE};
	if (!REQUIRE_SITES(kCalls, self->id))
		return;
	HOOK_SITE(SITE_SESSION_UPDATE, SessionUpdateHook, g_SessionUpdateOriginal, self->id);
}

/* Members die with the title. */
static VOID ResetNativeRemoteMembers()
{
	DWORD j = 0;
	while (j < MATCHMAKING_MAX_REMOTE_PEERS) {
		g_NativeJoined[j] = 0;
		g_NativeMissingSince[j] = 0;
		j++;
	}
	g_NativeJoinAttempts = 0;
	g_NativeJoinLastTick = 0;
}

static const PatchDescriptor kRows[] = {
	{"QNet native remote member", "native_remote_members", ON_TITLE,
		"Session::Update joins roster players as native members; missing 10 s is removed",
		InstallNativeRemoteMembers, ResetNativeRemoteMembers},
};
const PatchTable kNativeRemoteMemberPatches = PATCH_TABLE(kRows);
