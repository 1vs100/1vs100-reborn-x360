/* QNet on a local XSession, no Live. Synchronous create, null join stand-in, wait-order repairs. */
#include "../registry/PatchHelpers.h"
#include "../platform/Log.h"
#include "PatchTables.h"
#include "NativeRemoteMembers.h"
#include "TitleEvents.h"
#include "../title/TitleLayout.h"

/* Null handle: no XAM session to join. */
static DWORD WINAPI LocalXSessionCreate(DWORD flags, DWORD userIndex, DWORD maxPublicSlots,
	DWORD maxPrivateSlots, ULONGLONG *sessionNonce, PXSESSION_INFO sessionInfo,
	PXOVERLAPPED overlapped, HANDLE *sessionHandle)
{
	LOG_INFO("qnet", "LocalXSessionCreate");
	/* Entering a game. */
	TitleEvent_LocalGameEntered();
	DWORD i;
	if (userIndex >= XUSER_MAX_COUNT || sessionNonce == NULL || sessionInfo == NULL ||
		sessionHandle == NULL)
		return ERROR_INVALID_PARAMETER;

	*sessionHandle = NULL;
	ZeroMemory(sessionInfo, sizeof(*sessionInfo));
	for (i = 0; i < sizeof(sessionInfo->sessionID.ab); ++i)
		sessionInfo->sessionID.ab[i] = (BYTE)(i + 1);
	for (i = 0; i < sizeof(sessionInfo->keyExchangeKey.ab); ++i)
		sessionInfo->keyExchangeKey.ab[i] = (BYTE)(0xA0 + i);
	XNetGetTitleXnAddr(&sessionInfo->hostAddress);
	if (*sessionNonce == 0)
		*sessionNonce = 1;

	/* Register the fake key: later XNet calls fail without it. */
	{
		INT regRes = XNetRegisterKey(&sessionInfo->sessionID, &sessionInfo->keyExchangeKey);
		LOG_INFO("qnet", "liveless XSessionCreate XNetRegisterKey -> %d", regRes);
	}

	if (overlapped != NULL) {
		overlapped->InternalLow = ERROR_SUCCESS;
		overlapped->InternalHigh = 0;
		overlapped->InternalContext = 0;
		overlapped->dwExtendedError = ERROR_SUCCESS;
	}
	LOG_INFO("qnet",
		"liveless XSessionCreate flags=%08X user=%u public=%u private=%u nonce=%08X%08X", flags,
		userIndex, maxPublicSlots, maxPrivateSlots, (DWORD)(*sessionNonce >> 32),
		(DWORD)*sessionNonce);
	return ERROR_SUCCESS;
}

typedef int(__fastcall *SessionHostComplete_t)(DWORD session, int hr);
static SessionHostComplete_t g_HostCompleteOriginal = NULL;

/* Null handle skips the join and the flag; set the flag after success. */
static int __fastcall LocalHostCompleteHook(DWORD session, int hr)
{
	/* Return is the last listener call result. */
	int rc = g_HostCompleteOriginal(session, hr);
	volatile DWORD *flags = (volatile DWORD *)(session + Session::kFlags);
	if (hr >= 0 && *(volatile DWORD *)(session + Session::kXamHandle) == 0 &&
		*(volatile DWORD *)(session + Session::kType) != 0 &&
		*(volatile DWORD *)(session + Session::kPeerNet) != 0 &&
		!(*flags & Session::kFlagXSessionJoined)) {
		*flags |= Session::kFlagXSessionJoined;
		LOG_INFO("qnet", "liveless host complete: XSession-joined flag set, flags=%08X", *flags);
	}
	return rc;
}

/* Native members have no machine: the update fails. Log it; other consoles learn through matchmaking. */
typedef int(__fastcall *SessionUpdateRemotes_t)(DWORD session);
static SessionUpdateRemotes_t g_UpdateRemotesOriginal = NULL;
static volatile LONG g_UpdateRemotesFailures = 0;

static int __fastcall LocalUpdateRemotesHook(DWORD session)
{
	int hr = g_UpdateRemotesOriginal(session);
	if (hr < 0 && *(volatile DWORD *)(session + Session::kXamHandle) == 0) {
		if (Log_First(&g_UpdateRemotesFailures, 16))
			LOG_INFO("qnet",
				"liveless session: remote machine update failed hr=%08X, not ending the session",
				(DWORD)hr);
		return 0;
	}
	return hr;
}

typedef int(WINAPI *QNetLeaveErrorHandler_t)(DWORD wrapper, int errorCode, int unused, int flag);

/* Game-command block from the error handler load displacement. */
static DWORD CmdBlockOf(DWORD wrapper)
{
	return wrapper + (*(const DWORD *)TitleSiteAddress(SITE_QNET_CMD_BLOCK_LOAD) & 0xFFFFu);
}

/* Arm the wait before the call. */
static DWORD WINAPI LocalQNetLeaveHandler(DWORD wrapper, BYTE a2, BYTE a3, BYTE a4)
{
	LOG_DEBUG("qnet", "LocalQNetLeaveHandler");
	DWORD cqnetThis = *(DWORD *)(wrapper + QNetWrapper::kCQNet);
	DWORD vtable = *(DWORD *)cqnetThis;
	DWORD leaveGameFn = *(DWORD *)(vtable + CQNetVtbl::kLeaveGame);
	DWORD cmd = CmdBlockOf(wrapper);
	int result;

	NativeRemoveAllBeforeLeave(cqnetThis);
	/* Out of the game. */
	TitleEvent_LocalGameLeft();
	*(DWORD *)(cmd + QNetCmdBlock::kWaitState) = 0;
	*(DWORD *)(cmd + QNetCmdBlock::kWaitKind) = 8;

	result = ((int(WINAPI *)(DWORD, DWORD, DWORD, DWORD))leaveGameFn)(cqnetThis, a2, 0, 0);

	*(BYTE *)(cmd + QNetCmdBlock::kPending) = a4;
	*(BYTE *)(cmd + QNetCmdBlock::kLeaveByte) = 0;
	*(DWORD *)(cmd + QNetCmdBlock::kLeaveDword) = 0;
	if (a3 != 0)
		*(DWORD *)(wrapper + QNetWrapper::kField16) = 0;

	if (result < 0) {
		*(DWORD *)(cmd + QNetCmdBlock::kWaitKind) = 0;
		LOG_INFO("qnet", "QNet LeaveGame failed hr=%d, early arm undone", result);
		((QNetLeaveErrorHandler_t)TitleSiteAddress(SITE_QNET_LEAVE_ERROR_HANDLER))(
			wrapper, result, 0, 1);
	}
	return 1;
}

/* Synchronous completion fires the event inside the call: arm the wait first, undo on failure. */
static int RunQNetGameCommand(
	DWORD wrapper, DWORD slotOffset, DWORD armKind, DWORD armState, const char *name)
{
	DWORD cqnet = *(DWORD *)(wrapper + QNetWrapper::kCQNet);
	DWORD vtable = *(DWORD *)cqnet;
	DWORD cmd = CmdBlockOf(wrapper);
	int hr;

	*(BYTE *)(cmd + QNetCmdBlock::kPending) = 0;
	if (!((int(WINAPI *)(DWORD)) * (DWORD *)(vtable + CQNetVtbl::kIsHost))(cqnet)) {
		LOG_INFO("qnet", "QNet %s refused: not the host", name);
		((QNetLeaveErrorHandler_t)TitleSiteAddress(SITE_QNET_LEAVE_ERROR_HANDLER))(
			wrapper, -2135949243, 0, 1);
		return 1;
	}
	*(DWORD *)(cmd + QNetCmdBlock::kWaitKind) = armKind;
	*(DWORD *)(cmd + QNetCmdBlock::kWaitState) = armState;
	hr = ((int(WINAPI *)(DWORD)) * (DWORD *)(vtable + slotOffset))(cqnet);
	if (hr < 0) {
		*(DWORD *)(cmd + QNetCmdBlock::kWaitKind) = 0;
		LOG_INFO("qnet", "QNet %s failed hr=%08X, early arm undone", name, (DWORD)hr);
		((QNetLeaveErrorHandler_t)TitleSiteAddress(SITE_QNET_LEAVE_ERROR_HANDLER))(
			wrapper, hr, 0, 1);
	}
	return 1;
}

static int WINAPI LocalQNetStartGameHandler(DWORD wrapper)
{
	TitleEvent_LocalGameEntered();
	return RunQNetGameCommand(wrapper, CQNetVtbl::kStartGame, 3, 6, "StartGame");
}

static int WINAPI LocalQNetEndGameHandler(DWORD wrapper)
{
	return RunQNetGameCommand(wrapper, CQNetVtbl::kEndGame, 6, 3, "EndGame");
}

static VOID InstallUpdateRemotes(const PatchDescriptor *self)
{
	HOOK_SITE(SITE_SESSION_UPDATE_REMOTES, LocalUpdateRemotesHook, g_UpdateRemotesOriginal, self->id);
}

static VOID InstallHostComplete(const PatchDescriptor *self)
{
	HOOK_SITE(SITE_SESSION_HOST_COMPLETE, LocalHostCompleteHook, g_HostCompleteOriginal, self->id);
}

static VOID InstallXSessionCreate(const PatchDescriptor *self)
{
	InstallJumpSite(SITE_XSESSION_CREATE, (DWORD)LocalXSessionCreate, self->id);
}

/* Failed commands report through the leave error handler. */
static const SiteId kOrderCalls[] = {SITE_QNET_LEAVE_ERROR_HANDLER, SITE_QNET_CMD_BLOCK_LOAD};

static VOID InstallLeaveOrder(const PatchDescriptor *self)
{
	if (REQUIRE_SITES(kOrderCalls, self->id))
		InstallJumpSite(SITE_QNET_LEAVE_HANDLER, (DWORD)LocalQNetLeaveHandler, self->id);
}

static VOID InstallStartGameOrder(const PatchDescriptor *self)
{
	if (REQUIRE_SITES(kOrderCalls, self->id))
		InstallJumpSite(SITE_QNET_START_GAME_HANDLER, (DWORD)LocalQNetStartGameHandler, self->id);
}

static VOID InstallEndGameOrder(const PatchDescriptor *self)
{
	if (REQUIRE_SITES(kOrderCalls, self->id))
		InstallJumpSite(SITE_QNET_END_GAME_HANDLER, (DWORD)LocalQNetEndGameHandler, self->id);
}

static const PatchDescriptor kRows[] = {
	{"liveless XSessionCreate", "xsession_create", ON_TITLE,
		"XSessionCreate completes synchronously with a null handle and a registered fake key",
		InstallXSessionCreate},
	{"liveless XSession joined", "xsession_joined", ON_TITLE,
		"After a liveless host completion, sets the session's XSession-joined flag",
		InstallHostComplete},
	{"liveless remote update", "remote_update", ON_TITLE,
		"A failed update of the remote machines (native members) does not end the liveless session",
		InstallUpdateRemotes},
	{"QNet leave ordering", "qnet_leave_order", ON_TITLE,
		"LeaveGame removes native members and arms the wrapper's wait before the call",
		InstallLeaveOrder},
	{"QNet StartGame ordering", "qnet_startgame_order", ON_TITLE,
		"StartGame arms the wrapper's wait before the call (the event fires inside it)",
		InstallStartGameOrder},
	{"QNet EndGame ordering", "qnet_endgame_order", ON_TITLE,
		"EndGame arms the wrapper's wait before the call (the event fires inside it)",
		InstallEndGameOrder},
};
const PatchTable kLivelessSessionPatches = PATCH_TABLE(kRows);
