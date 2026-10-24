/* Local profiles read as Live-enabled Gold with every privilege. */
#include "PatchTables.h"
#include "../registry/PatchHelpers.h"
#include "../config/PatchSwitches.h"
#include "../platform/ImportHook.h"
#include "../platform/Log.h"

#define MEMBERSHIP_TYPE_GOLD 6

extern "C" void *_ReturnAddress(void);
#pragma intrinsic(_ReturnAddress)

/* Online-only queries fail for local profiles; answer with the offline XUID. */
#define XUID_TYPE_OFFLINE 1u
#define XUID_TYPE_ONLINE 2u

static HRESULT XamUserGetXUIDHook(DWORD userIndex, DWORD type, PXUID out)
{
	HRESULT hr = XamUserGetXUID(userIndex, type, out);
	if (hr == HRESULT_FROM_WIN32(ERROR_NO_SUCH_USER) && type == XUID_TYPE_ONLINE &&
		!LoaderPatchDisabled("offlinexuid")) {
		static volatile LONG hits;
		hr = XamUserGetXUID(userIndex, XUID_TYPE_OFFLINE, out);
		if (Log_First(&hits, 5))
			LOG_INFO("xam",
				"[AUDIT] XamUserGetXUID online->offline user=%u caller=%08X hr=%08X xuid=%08X%08X",
				userIndex, (DWORD)_ReturnAddress(), hr,
				(SUCCEEDED(hr) && out) ? (DWORD)(*out >> 32) : 0,
				(SUCCEEDED(hr) && out) ? (DWORD)*out : 0);
	}
	return hr;
}

static XAMUSER_SIGNIN_STATE XamUserGetSigninStateHook(DWORD dwUserIndex)
{
	XAMUSER_SIGNIN_STATE state = XamUserGetSigninState(dwUserIndex);

	switch (state) {
	case eXamUserSigninState_SignedInLocally:
		return eXamUserSigninState_SignedInToLive;
	default:
		return state;
	}
}

static BOOL XamUserIsOnlineEnabledHook(IN DWORD dwUserIndex)
{
	(void)dwUserIndex;
	return TRUE;
}

static int XamUserGetSigninInfoHook(
	IN DWORD userIndex, IN DWORD flags, IN OUT PXUSER_SIGNIN_INFO xSigningInfo)
{
	int ret = XamUserGetSigninInfo(userIndex, flags, xSigningInfo);
	// Zero XUID for a local profile: use the offline XUID.
	if (ret == ERROR_SUCCESS && xSigningInfo && xSigningInfo->xuid == 0 &&
		!LoaderPatchDisabled("zeroxuid") &&
		xSigningInfo->UserSigninState != eXUserSigninState_NotSignedIn) {
		static volatile LONG zeroHits;
		if (Log_First(&zeroHits, 5))
			LOG_INFO("xam", "[AUDIT] zeroxuid used: user=%u flags=%08X", userIndex, flags);
		ret =
			XamUserGetSigninInfo(userIndex, XUSER_GET_SIGNIN_INFO_OFFLINE_XUID_ONLY, xSigningInfo);
	}
	if (ret == ERROR_SUCCESS && xSigningInfo && !LoaderPatchDisabled("liveflag") &&
		xSigningInfo->UserSigninState != eXUserSigninState_NotSignedIn) {
		{
			static volatile LONG flagHits;
			if (!(xSigningInfo->dwInfoFlags & XUSER_INFO_FLAG_LIVE_ENABLED) ||
				xSigningInfo->UserSigninState == eXUserSigninState_SignedInLocally) {
				if (Log_First(&flagHits, 5))
					LOG_INFO("xam", "[AUDIT] liveflag changed: infoFlags=%08X state=%u",
						xSigningInfo->dwInfoFlags, (DWORD)xSigningInfo->UserSigninState);
			}
		}
		xSigningInfo->dwInfoFlags |= XUSER_INFO_FLAG_LIVE_ENABLED;
		if (xSigningInfo->UserSigninState == eXUserSigninState_SignedInLocally)
			xSigningInfo->UserSigninState = eXUserSigninState_SignedInToLive;
	}
	return ret;
}

static int XamUserCheckPrivilegeHook(
	IN DWORD dwUserIndex, IN DWORD PrivilegeType, OUT PBOOL pfResult)
{
	(void)dwUserIndex;
	(void)PrivilegeType;
	if (pfResult)
		*pfResult = TRUE;
	return 0;
}

static int XamUserGetMembershipTypeFromXUIDHook(XUID inx)
{
	(void)inx;
	return MEMBERSHIP_TYPE_GOLD;
}

static int XamUserGetMembershipTypeHook(DWORD idx)
{
	(void)idx;
	return MEMBERSHIP_TYPE_GOLD;
}

/* Clear SHOWONLYONLINEENABLED: the picker would list no local profile. Resolved by ordinal. */
#define XAM_SHOW_SIGNIN_UI_ORDINAL 700u
typedef DWORD(NTAPI *XamShowSigninUI_t)(DWORD cPanes, DWORD flags);
typedef char XssuiShowOnlyOnlineEnabledIs2[XSSUI_FLAGS_SHOWONLYONLINEENABLED == 2 ? 1 : -1];

static DWORD NTAPI XamShowSigninUIHook(DWORD cPanes, DWORD flags)
{
	static XamShowSigninUI_t original = NULL;
	if (!original)
		original = (XamShowSigninUI_t)ResolveFunction("xam.xex", XAM_SHOW_SIGNIN_UI_ORDINAL);
	if (!original)
		return ERROR_FUNCTION_FAILED;
	LOG_INFO("xam", "XamShowSigninUI panes=%u flags=%08X -> %08X", cPanes, flags,
		flags & ~XSSUI_FLAGS_SHOWONLYONLINEENABLED);
	return original(cPanes, flags & ~XSSUI_FLAGS_SHOWONLYONLINEENABLED);
}

static const PatchDescriptor kRows[] = {
	{"XamUserGetSigninState", "signin_state", ON_TITLE,
		"Signed in locally reads as signed in to Live", InstallXamImport, NULL, 528,
		(PVOID)XamUserGetSigninStateHook},
	{"XamUserCheckPrivilege", "check_privilege", ON_TITLE, "Every privilege check passes",
		InstallXamImport, NULL, 530, (PVOID)XamUserCheckPrivilegeHook},
	{"XamUserGetMembershipTypeFromXUID", "membership_from_xuid", ON_TITLE,
		"Every XUID is a Gold member", InstallXamImport, NULL, 535,
		(PVOID)XamUserGetMembershipTypeFromXUIDHook},
	{"XamUserGetMembershipType", "membership", ON_TITLE, "Every local user is a Gold member",
		InstallXamImport, NULL, 539, (PVOID)XamUserGetMembershipTypeHook},
	{"XamUserIsOnlineEnabled", "online_enabled", ON_TITLE, "Every user is online-enabled",
		InstallXamImport, NULL, 545, (PVOID)XamUserIsOnlineEnabledHook},
	{"XamUserGetXUID", "get_xuid", ON_TITLE,
		"Online-only XUID queries answer with the offline XUID (switch offlinexuid)",
		InstallXamImport, NULL, 522, (PVOID)XamUserGetXUIDHook},
	{"XamUserGetSigninInfo", "signin_info", ON_TITLE,
		"Sign-in info: offline XUID for a zero XUID, LIVE_ENABLED flag (switches zeroxuid, liveflag)",
		InstallXamImport, NULL, 551, (PVOID)XamUserGetSigninInfoHook},
	{"XamShowSigninUI", "signin_ui_local", ON_TITLE,
		"The sign-in picker also lists local profiles (no SHOWONLYONLINEENABLED)", InstallXamImport,
		NULL, XAM_SHOW_SIGNIN_UI_ORDINAL, (PVOID)XamShowSigninUIHook},
};
const PatchTable kLiveIdentityPatches = PATCH_TABLE(kRows);
