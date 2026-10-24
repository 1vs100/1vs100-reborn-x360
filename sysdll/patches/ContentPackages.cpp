/* Content-package lookups on a loose kernel. */
#include "../registry/PatchHelpers.h"
#include "../platform/Log.h"
#include "PatchTables.h"
#include "ContentDirectoryScan.h"
#include "../platform/Ppc.h"

/* Grid sends 40 hex digits; the native chain doubles them to 82, which Package rejects. Push the text unchanged. */
static VOID InstallContentIdPassthrough(const PatchDescriptor *self)
{
	/* 1317 only; other profiles turn it off. */
	static const BYTE kZero[28] = {0};
	BYTE cave[28];
	BYTE site[4];
	static const SiteId kTargets[] = {SITE_TEXT_PADDING_CAVE, SITE_CONTENT_ID_PUSH_SITE,
		SITE_LUA_PUSH_STRING, SITE_LUA_SET_FIELD, SITE_CONTENT_ID_PUSH_RET};
	const DWORD c = TitleSiteAddress(SITE_TEXT_PADDING_CAVE) + 0x30u;

	if (!REQUIRE_SITES(kTargets, self->id))
		return;
	if (!Ppc_InBranchRange(TitleSiteAddress(SITE_CONTENT_ID_PUSH_SITE), c) ||
		!Ppc_InBranchRange(c + 4, TitleSiteAddress(SITE_LUA_PUSH_STRING)) ||
		!Ppc_InBranchRange(c + 20, TitleSiteAddress(SITE_LUA_SET_FIELD)) ||
		!Ppc_InBranchRange(c + 24, TitleSiteAddress(SITE_CONTENT_ID_PUSH_RET))) {
		ReportPatch(self->id, "failed", TitleSiteAddress(SITE_CONTENT_ID_PUSH_SITE), "range");
		return;
	}
	Ppc_PutWordBE(site, Ppc_Branch(TitleSiteAddress(SITE_CONTENT_ID_PUSH_SITE), c, FALSE));
	Ppc_PutWordBE(cave + 0, 0x7CA42B78u); /* mr   r4, r5      (text ptr) */
	Ppc_PutWordBE(cave + 4,
		Ppc_Branch(
			c + 4, TitleSiteAddress(SITE_LUA_PUSH_STRING), TRUE)); /* bl   lua_pushstring       */
	Ppc_PutWordBE(cave + 8, 0x7FE3FB78u);                          /* mr   r3, r31     (L)       */
	Ppc_PutWordBE(cave + 12, 0x3880FFFEu);                         /* li   r4, -2                */
	Ppc_PutWordBE(cave + 16, 0x7E85A378u);                         /* mr   r5, r20     (name)    */
	Ppc_PutWordBE(cave + 20,
		Ppc_Branch(
			c + 20, TitleSiteAddress(SITE_LUA_SET_FIELD), TRUE)); /* bl   lua_setfield          */
	Ppc_PutWordBE(cave + 24,
		Ppc_Branch(c + 24, TitleSiteAddress(SITE_CONTENT_ID_PUSH_RET),
			FALSE)); /* b    return                */

	const SiteSpec *push = TitleSite(SITE_CONTENT_ID_PUSH_SITE);
	const DWORD pushSite = TitleSiteAddress(SITE_CONTENT_ID_PUSH_SITE);
	if (push == NULL || push->expect == NULL ||
		!SiteAccepts(pushSite, push->expect, push->expectSize, site, sizeof(site)) ||
		!SiteAccepts(c, kZero, sizeof(kZero), cave, sizeof(cave))) {
		LOG_INFO("content",
			"ContentId pass-through: signature mismatch at site %08X or cave %08X, no write",
			pushSite, c);
		ReportPatchMismatch(self->id, pushSite);
		return;
	}
	/* Cave first. */
	if (!EnsurePatch("ContentId pass-through cave", c, kZero, sizeof(kZero), cave, sizeof(cave)) ||
		!ApplyPokeSite(
			SITE_CONTENT_ID_PUSH_SITE, site, sizeof(site), "ContentId pass-through call site"))
		return;
	LOG_INFO("content",
		"ContentId pass-through installed: site %08X -> cave %08X (Offers.ContentId = Grid text)",
		TitleSiteAddress(SITE_CONTENT_ID_PUSH_SITE), c);
}

/* Downloaded names are 42 chars; pack names are 40, compared on the first 40. Accept either. */
static const BYTE kContentNameLenPatched[12] = {
	0x39, 0x6B, 0xFF, 0xD8, 0x71, 0x60, 0xFF, 0xFD, 0x41, 0x82, 0x00, 0x20};

static VOID InstallContentNameLengthFix(const PatchDescriptor *self)
{
	/* Same scope as the pass-through. */
	if (!ApplyPokeSite(SITE_CONTENT_NAME_LEN, kContentNameLenPatched,
			sizeof(kContentNameLenPatched), self->id))
		return;
	LOG_INFO("content",
		"ContentId name-length fix installed at %08X (szFileName length 42 -> 40 or 42)",
		TitleSiteAddress(SITE_CONTENT_NAME_LEN));
}

/* dwTitleId is hard-coded 0, which does not resolve here. Cave loads the profile content id. */
static const BYTE kTitleIdCave2Original[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static VOID InstallContentTitleIdFix(const PatchDescriptor *self)
{

	const DWORD contentTitleId = TitleContentTitleId();
	const DWORD cave2 = TitleSiteAddress(SITE_TEXT_PADDING_CAVE) + 0x10;
	const SiteSpec *wrapperSite = TitleSite(SITE_CONTENT_TITLE_ID_WRAPPER);
	const DWORD wrapper = TitleSiteAddress(SITE_CONTENT_TITLE_ID_WRAPPER);
	BYTE wrapperPatched[4]; /* b cave2 */
	BYTE cavePatched[12];   /* r6 = contentTitleId, continue after the site */
	if (!Ppc_InBranchRange(wrapper, cave2) || !Ppc_InBranchRange(cave2 + 8, wrapper + 4)) {
		ReportPatch(self->id, "failed", cave2, "range");
		return;
	}
	Ppc_PutWordBE(wrapperPatched, Ppc_Branch(wrapper, cave2, FALSE));
	Ppc_PutWordBE(cavePatched + 0, 0x3CC00000u | (contentTitleId >> 16));    /* lis r6, hi */
	Ppc_PutWordBE(cavePatched + 4, 0x60C60000u | (contentTitleId & 0xFFFF)); /* ori r6, r6, lo */
	Ppc_PutWordBE(
		cavePatched + 8, Ppc_Branch(cave2 + 8, wrapper + 4, FALSE)); /* b next: ld r3, var_50(r1) */
	if (!SiteAccepts(cave2, kTitleIdCave2Original, sizeof(kTitleIdCave2Original), cavePatched,
			sizeof(cavePatched))) {
		LOG_INFO(
			"content", "ContentId TitleID fix: cave signature mismatch at %08X, no write", cave2);
		ReportPatchMismatch("ContentId TitleID fix cave2", cave2);
		return;
	}
	if (wrapperSite == NULL || wrapperSite->expect == NULL ||
		!SiteAccepts(wrapper, wrapperSite->expect, wrapperSite->expectSize, wrapperPatched,
			sizeof(wrapperPatched))) {
		LOG_INFO("content",
			"ContentId TitleID fix: wrapper call-site signature mismatch at %08X, no write",
			wrapper);
		ReportPatchMismatch("ContentId TitleID fix wrapper call site", wrapper);
		return;
	}
	/* Cave first. */
	if (!EnsurePatch("ContentId TitleID fix cave2", cave2, kTitleIdCave2Original,
			sizeof(kTitleIdCave2Original), cavePatched, sizeof(cavePatched)) ||
		!ApplyPokeSite(SITE_CONTENT_TITLE_ID_WRAPPER, wrapperPatched, sizeof(wrapperPatched),
			"ContentId TitleID fix wrapper call site"))
		return;
	LOG_INFO("content",
		"ContentId TitleID fix installed: cave@%08X, wrapper site@%08X redirected (dwTitleId 0 -> %08X)",
		cave2, wrapper, contentTitleId);
}

/* DeviceId 0 matches no device; the wildcard is -1. Same-size patch. */
static const BYTE kContentDeviceIdPatched[4] = {
	0x38, 0x80, 0xFF, 0xFF}; /* XCONTENTDEVICE_ALL */

static VOID InstallContentDeviceIdFix(const PatchDescriptor *self)
{
	if (!ApplyPokeSite(SITE_CONTENT_DEVICE_ID_ARG, kContentDeviceIdPatched,
			sizeof(kContentDeviceIdPatched), self->id))
		return;
	LOG_INFO("content",
		"ContentId DeviceId fix installed at %08X (li r4,0 -> li r4,-1 / XCONTENTDEVICE_ALL)",
		TitleSiteAddress(SITE_CONTENT_DEVICE_ID_ARG));
}

/* XAM listing returns no items here; answer message 0x2000E from the real files. */
#define XAM_MSG_CONTENT_ENUMERATE 0x2000Eu
#define CONTENT_ENUM_NOT_FOUND_HR ((HRESULT)0x80070012L)
#define CONTENT_TYPE_MARKETPLACE 2u

/* Real files of the mounted content directory; names unchanged. Each enumerator gets every file once. */
static volatile LONG g_ContentFallbackEnabled = 0;

/* No hook of its own: ordinal 500 has one slot, owned below. */
static HRESULT ContentEnumerationFallback(DWORD dwMessage, UINT_PTR param1, HRESULT hr)
{
	if (!g_ContentFallbackEnabled || dwMessage != XAM_MSG_CONTENT_ENUMERATE ||
		hr != CONTENT_ENUM_NOT_FOUND_HR)
		return hr;

	/* msgBuf[3] is the 320-byte item buffer. */
	DWORD *msgBuf = (DWORD *)param1;
	if (msgBuf == NULL)
		return hr;
	BYTE *itemBuf = (BYTE *)msgBuf[3];
	if (itemBuf == NULL)
		return hr;

	DWORD execTitleId = 0;
	if (!ResolveCurrentTitleId(&execTitleId)) {
		LOG_INFO(
			"content", "Content enumeration fallback: XamGetExecutionId failed, aborting fallback");
		return hr;
	}

	ContentFallbackItem items[CONTENT_FALLBACK_MAX_ITEMS];
	DWORD itemCount = ScanContentDirectory(execTitleId, items, CONTENT_FALLBACK_MAX_ITEMS);
	if (itemCount == 0)
		return hr;

	DWORD deviceId = 0;
	if (!ResolveHddDeviceId(&deviceId))
		return hr;

	DWORD candidateIndex;
	SpinLock_LockSleeping(&g_ContentEnumLock);
	{
		ContentEnumCursor *cursor = GetContentCursor(msgBuf[2]);
		candidateIndex = cursor->next++;
		if (candidateIndex >= itemCount) {
			cursor->key = 0; /* end of listing: free the slot */
			SpinLock_Unlock(&g_ContentEnumLock);
			LOG_INFO("content",
				"Content enumeration fallback: end of listing (%u item(s)) - reporting no more files",
				itemCount);
			return hr;
		}
	}
	SpinLock_Unlock(&g_ContentEnumLock);

	XMemSet(itemBuf, 0, 320);
	*(DWORD *)(itemBuf + 0) = deviceId;                 /* XCONTENT_DATA.DeviceID */
	*(DWORD *)(itemBuf + 4) = CONTENT_TYPE_MARKETPLACE; /* XCONTENT_DATA.dwContentType */
	/* Display name left zeroed: cosmetic only. */
	memcpy(itemBuf + 0x108, items[candidateIndex].fileName,
		sizeof(items[candidateIndex].fileName)); /* XCONTENT_DATA.szFileName[42] */

	/* v12 past the buffer: execution id, not header title id. */
	*(DWORD *)(itemBuf + 320) = execTitleId;

	CHAR shown[43];
	memcpy(shown, items[candidateIndex].fileName, 42);
	shown[42] = 0;
	LOG_INFO("content",
		"Content enumeration fallback: item %u/%u '%s' (DeviceID=%08X, v12=%08X) - broken XamEnumerate bypassed",
		candidateIndex + 1, itemCount, shown, deviceId, execTitleId);
	return S_OK;
}

/* Only hook on ordinal 500: an import redirects once. Also logs XLSP lookups and every message once. */
extern "C" void *_ReturnAddress(void);
#pragma intrinsic(_ReturnAddress)

static LogSeenSet g_SeenMessages;

static DWORD XMsgInProcessCallHook(HXAMAPP app, DWORD msg, UINT_PTR param1, UINT_PTR param2)
{
	const DWORD caller = (DWORD)_ReturnAddress();
	DWORD rc = (DWORD)XMsgInProcessCall(app, msg, param1, param2);
	rc = (DWORD)ContentEnumerationFallback(msg, param1, (HRESULT)rc);
	if ((DWORD)app == 0xFC && msg == 0x58007) {
		LOG_DEBUG("netlog", "XLSP lookup service=%08X caller=%08X rc=%08X addr=%08X", (DWORD)param1,
			caller, rc, (rc < 0x80000000u && param2) ? ((DWORD *)param2)[1] : 0);
	} else if (Log_FirstSeen(&g_SeenMessages, ((DWORD)app << 24) ^ msg)) {
		LOG_DEBUG("netlog", "XMsgInProcessCall first app=%08X msg=%08X caller=%08X rc=%08X",
			(DWORD)app, msg, caller, rc);
	}
	return rc;
}

static VOID InstallContentEnumerationFallback(const PatchDescriptor *self)
{
	InterlockedExchange(&g_ContentFallbackEnabled, 1);
	ReportPatch(self->id, "installed", 500, "dispatch");
}

static VOID ResetContentEnumerationFallback()
{
	InterlockedExchange(&g_ContentFallbackEnabled, 0);
	ResetContentCursors();
}

static const PatchDescriptor kRows[] = {
	{"XMsgInProcessCall", "xmsg_inprocess_call", ON_TITLE,
		"Dispatches XMsgInProcessCall results to the enumeration fallback; logs XLSP lookups",
		InstallXamImport, NULL, 500, (PVOID)XMsgInProcessCallHook},
	{"ContentId pass-through", "contentid_passthrough", ON_TITLE,
		"Offers.ContentId gets the Grid's 40-digit text unchanged (1317's parser re-encodes it)",
		InstallContentIdPassthrough},
	{"ContentId name-length fix", "content_name_length", ON_TITLE,
		"Package():IsPresentQuery accepts 40- or 42-character package file names (1317)",
		InstallContentNameLengthFix},
	{"ContentId TitleID fix", "content_titleid", ON_TITLE,
		"The content enumerator wrapper passes the content title id instead of 0 (1317)",
		InstallContentTitleIdFix},
	{"ContentId DeviceId fix", "content_deviceid", ON_TITLE,
		"Package():GetDeviceID enumerates XCONTENTDEVICE_ALL instead of device 0 (1317)",
		InstallContentDeviceIdFix},
	{"Content enumeration fallback", "content_fallback", ON_TITLE,
		"A failed content enumeration (0x2000E, 0x80070012) is answered from the real package files",
		InstallContentEnumerationFallback, ResetContentEnumerationFallback},
};
const PatchTable kContentPackagePatches = PATCH_TABLE(kRows);
