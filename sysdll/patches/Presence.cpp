/* Preset presence completes at once; the rich-presence context drives the roster. In-game context on any join path puts the console back on the roster. */
#include "../registry/PatchHelpers.h"
#include "../platform/Log.h"
#include "../platform/ImportHook.h"
#include "PatchTables.h"
#include "TitleEvents.h"

#define XAM_APP_PRESENCE_CONTEXT 0xFBu
#define XAM_MSG_USER_SET_CONTEXT 0xB0006u
/* 0 is the lobby. */
#define PRESENCE_LOBBY 0u

typedef DWORD(NTAPI *XMsgStartIORequest_t)(
	DWORD app, DWORD msg, PXOVERLAPPED ov, PVOID buf, DWORD size);

static DWORD NTAPI XMsgStartIORequestHook(
	DWORD app, DWORD msg, PXOVERLAPPED ov, PVOID buf, DWORD size)
{
	static XMsgStartIORequest_t orig = NULL;
	if (!orig)
		orig = (XMsgStartIORequest_t)ResolveFunction("xam.xex", 503);
	if (app == XAM_APP_PRESENCE_CONTEXT && msg == XAM_MSG_USER_SET_CONTEXT && buf && size >= 0x18) {
		const DWORD *w = (const DWORD *)buf;
		/* Words: user, ptr, 0, 0, contextId, value. Non-lobby means in game. */
		if (w[4] == X_CONTEXT_PRESENCE && w[5] != PRESENCE_LOBBY) {
			LOG_INFO("mm", "[PRESENCE] in game (value %u): back on the roster", w[5]);
			TitleEvent_LocalGameEntered();
		}
	}
	return orig ? orig(app, msg, ov, buf, size) : 0x80004005;
}

/* Real call blocks on dead Live, then crashes: no-op it. */
static DWORD XamSetUserPresetPresenceStateHook(
	DWORD dwUserIndex, DWORD dwPresenceState, PXOVERLAPPED pOverlapped)
{
	(void)dwUserIndex;
	(void)dwPresenceState;
	LOG_INFO("xam", "XamSetUserPresetPresenceState: no-op (ordinal 532)");
	if (pOverlapped) {
		pOverlapped->InternalLow = ERROR_SUCCESS;
		pOverlapped->InternalHigh = 1;
		pOverlapped->InternalContext = (ULONG_PTR)GetCurrentThread();
		pOverlapped->dwExtendedError = 0;
		if (pOverlapped->hEvent)
			SetEvent(pOverlapped->hEvent);
	}
	return ERROR_SUCCESS;
}

static const PatchDescriptor kRows[] = {
	{"XamSetUserPresetPresenceState", "preset_presence", ON_TITLE,
		"Preset presence (SetUserPresenceBusy) completes at once instead of waiting on Live",
		InstallXamImport, NULL, 532, (PVOID)XamSetUserPresetPresenceStateHook},
	{"XMsgStartIORequest (presence roster)", "presence_roster", ON_TITLE,
		"An in-game presence context puts the console back on the matchmaking roster",
		InstallXamImport, NULL, 503, (PVOID)XMsgStartIORequestHook},
};
const PatchTable kPresencePatches = PATCH_TABLE(kRows);
