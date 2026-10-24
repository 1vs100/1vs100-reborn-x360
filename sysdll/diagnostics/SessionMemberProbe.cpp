/* Member unlink watch, 1149. Log-only. Every removal goes through the unlink. */
#include "DiagnosticTables.h"
#include "../registry/PatchHelpers.h"
#include "../platform/Log.h"
#include "../title/TitleLayout.h"

typedef VOID(__fastcall *SessionMemberUnlink_t)(DWORD session, DWORD member, DWORD flag);
static SessionMemberUnlink_t g_SessionMemberUnlinkOriginal = NULL;

/* XUID at member+0x10. */
static VOID __fastcall SessionMemberUnlinkHook(DWORD session, DWORD member, DWORD flag)
{
	ULONGLONG xuid = 0;
	DWORD flags = 0, count = 0;
	if (member) {
		xuid = *(volatile ULONGLONG *)(member + 0x10);
		flags = *(volatile DWORD *)(member + Member::kFlags);
	}
	if (session)
		count = *(volatile DWORD *)(session + Session::kMemberCount);
	LOG_INFO("qnet",
		"session member unlink: member %08X xuid %08X%08X flags %08X%s flag %u session %08X count %u",
		member, (DWORD)(xuid >> 32), (DWORD)xuid, flags,
		(flags & Member::kFlagLocal) ? " local" : " remote", flag, session, count);
	g_SessionMemberUnlinkOriginal(session, member, flag);
}

static VOID InstallSessionMemberUnlinkLog(const PatchDescriptor *self)
{
	HOOK_SITE(SITE_SESSION_MEMBER_UNLINK, SessionMemberUnlinkHook, g_SessionMemberUnlinkOriginal,
		self->id);
}

static const PatchDescriptor kRows[] = {
	{"session member unlink log", "session_member_unlink_log", ON_EARLY | ON_TITLE,
		"Logs every member removal: xuid, local/remote, count",
		InstallSessionMemberUnlinkLog},
};
const PatchTable kSessionMemberProbePatches = PATCH_TABLE(kRows);
