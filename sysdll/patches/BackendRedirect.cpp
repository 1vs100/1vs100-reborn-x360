/* Points server resolution and Grid keys at the LAN backend. */
#include "PatchTables.h"
#include "../registry/PatchHelpers.h"
#include "../config/RevivalConfig.h"
#include "../platform/Log.h"
#include "../platform/Text.h"

static int TitleXNetServerToInAddrStub(IN_ADDR ina, DWORD serviceId, IN_ADDR *out)
{
	LOG_DEBUG("net", "TitleXNetServerToInAddr");
	(void)serviceId;
	if (out)
		out->S_un.S_addr = ina.S_un.S_addr;
	return 0;
}

/* r3=obj, r4=output; IPv4 at output+4. */
static int TitleRawResolveStub(void *obj, DWORD *output)
{
	LOG_DEBUG("net", "TitleRawResolve");
	(void)obj;
	if (output)
		output[1] = g_RevivalInAddr;
	return 0;
}

/* Only service 0x584A07DB is answered; the rest go to XAM. */
#define TITLE_XLSP_SERVICE_ID 0x584A07DBu
typedef int (*XlspLookup_t)(DWORD serviceId, DWORD *out);
static XlspLookup_t g_XlspLookupOriginal = NULL;

extern "C" void *_ReturnAddress(void);
#pragma intrinsic(_ReturnAddress)

/* Logged to see which other services the title asks for. */
static int XlspLookupHook(DWORD serviceId, DWORD *out)
{
	const DWORD caller = (DWORD)_ReturnAddress();
	int rc;

	if (serviceId == TITLE_XLSP_SERVICE_ID) {
		if (out)
			out[1] = g_RevivalInAddr;
		LOG_DEBUG("netlog", "XlspLookup service=%08X caller=%08X -> backend %08X", serviceId,
			caller, g_RevivalInAddr);
		return 0;
	}
	rc = g_XlspLookupOriginal ? g_XlspLookupOriginal(serviceId, out) : 1627;
	LOG_DEBUG("netlog", "XlspLookup service=%08X caller=%08X -> XAM rc=%d addr=%08X", serviceId,
		caller, rc, (rc == 0 && out) ? out[1] : 0);
	return rc;
}

/* LAN host: the call becomes li r3,0 (success). */
static const BYTE kLiR3Zero[4] = {0x38, 0x60, 0x00, 0x00};

/* The NTP skip depends on the lookup hook. Only builds with the sites report. */
static VOID InstallXlspLookup(const PatchDescriptor *self)
{
	if (TitleSite(SITE_XLSP_LOOKUP) &&
		!HOOK_SITE(SITE_XLSP_LOOKUP, XlspLookupHook, g_XlspLookupOriginal, self->id))
		return;
	if (TitleSite(SITE_NTP_XNET_CONNECT_CALL))
		ApplyPokeSite(
			SITE_NTP_XNET_CONNECT_CALL, kLiR3Zero, sizeof(kLiR3Zero), "NTP XNetConnect skip");
}

static VOID InstallXNetServerToInAddr(const PatchDescriptor *self)
{
	InstallJumpSite(SITE_XNET_SERVER_TO_IN_ADDR, (DWORD)TitleXNetServerToInAddrStub, self->id);
}

/* Both stubs stay: different callers. */
static VOID InstallSbgRawResolve(const PatchDescriptor *self)
{
	InstallJumpSite(SITE_SBG_RAW_RESOLVE, (DWORD)TitleRawResolveStub, self->id);
}

/* Same contract as the raw resolver: same stub. Port 1000 left as is. */
static VOID InstallSbgSessionResolve(const PatchDescriptor *self)
{
	if (TitleSite(SITE_SBG_SESSION_RESOLVE))
		InstallJumpSite(SITE_SBG_SESSION_RESOLVE, (DWORD)TitleRawResolveStub, self->id);
}

/* Built-in URL needs Live DNS; point it at the backend host. Only the host name is used, never a bare IP. */
/* FALSE when the host is empty or would not fit. */
static BOOL BuildGridUrl(const CHAR *host, CHAR *url, DWORD cap)
{
	static const CHAR prefix[] = "http://";
	static const CHAR suffix[] = "/grid/v1";
	DWORD hostLen = Text_Length(host, REVIVAL_HOST_BYTES);
	DWORD pos = 0;

	if (hostLen == 0)
		return FALSE;
	if (sizeof(prefix) - 1 + hostLen + sizeof(suffix) - 1 >= cap)
		return FALSE;
	XMemSet(url, 0, cap);
	pos = Text_Append(url, pos, cap, prefix);
	pos = Text_Append(url, pos, cap, host);
	pos = Text_Append(url, pos, cap, suffix);
	url[pos] = 0;
	return TRUE;
}

static VOID InstallGridUrl(const PatchDescriptor *self)
{
	CHAR url[32];

	if (!TitleSiteAddress(SITE_GRID_DEFAULT_URL)) {
		ReportPatch(self->id, "skipped", 0, "site");
		return;
	}
	if (!BuildGridUrl(g_RevivalHost, url, sizeof(url))) {
		LOG_ERROR("net", "Grid default URL: host \"%s\" missing or too long, leaving built-in",
			g_RevivalHost);
		ReportPatch(self->id, "skipped", 0, "config");
		return;
	}
	LOG_INFO("net", "Grid default URL -> %s", url);
	ApplyPokeSite(SITE_GRID_DEFAULT_URL, url, sizeof(url), self->id);
}

/* 1317 only. Check both originals first; never a partial key patch. */
static const char kGridLanModulus[] =
	"sI/yGKekfXLHDqOiRguTKWZ1XXYF6zphB8iY0dXcXtPGYoxqF7nhrbajRRwT6jMkjNXYF2eYfVePK/heQ7C8D5F2gYTcPQNWP7gFw6VQzdSofbD1kFKbmrpxVE0A5upUpac2kHs/BGB61+BcA8cvEQgdwMwjDNS5kc91tb9L5DE=";
typedef char GridLanModulusMustBe172Chars[(sizeof(kGridLanModulus) == 173) ? 1 : -1];

/* TRUE for original or LAN key. */
static BOOL KeySlotAccepts(const SiteSpec *slot)
{
	return slot != NULL && slot->expect != NULL && slot->expectSize == sizeof(kGridLanModulus) &&
		SiteAccepts(slot->address, slot->expect, slot->expectSize, kGridLanModulus,
			sizeof(kGridLanModulus));
}

static VOID InstallGridPublicKeys(const PatchDescriptor *self)
{
	static const SiteId kSlots[] = {SITE_GRID_PUBKEY_A, SITE_GRID_PUBKEY_B};
	if (!REQUIRE_SITES(kSlots, self->id))
		return;
	const SiteSpec *slotA = TitleSite(SITE_GRID_PUBKEY_A), *slotB = TitleSite(SITE_GRID_PUBKEY_B);
	const DWORD a = TitleSiteAddress(SITE_GRID_PUBKEY_A), b = TitleSiteAddress(SITE_GRID_PUBKEY_B);
	if (!KeySlotAccepts(slotA) || !KeySlotAccepts(slotB)) {
		LOG_INFO("net", "GRID RSA public keys: string mismatch at %08X or %08X, no writes", a, b);
		ReportPatchMismatch(self->id, a);
		return;
	}
	if (!ApplyPokeSite(SITE_GRID_PUBKEY_A, kGridLanModulus, sizeof(kGridLanModulus),
			"GRID RSA public key A") ||
		!ApplyPokeSite(
			SITE_GRID_PUBKEY_B, kGridLanModulus, sizeof(kGridLanModulus), "GRID RSA public key B"))
		return;
	LOG_INFO("net",
		"GRID RSA public keys installed at %08X and %08X (172 base64 chars, exponent 65537)", a, b);
}

static const PatchDescriptor kRows[] = {
	{"XNetServerToInAddr", "xnet_server_to_inaddr", ON_TITLE,
		"The title's XNetServerToInAddr wrapper returns the address it was given",
		InstallXNetServerToInAddr},
	{"SbgRawResolve", "sbg_raw_resolve", ON_TITLE,
		"The SBG raw resolver answers with the backend address", InstallSbgRawResolve},
	{"SbgSessionResolve", "sbg_session_resolve", ON_TITLE,
		"RegisterMasterUser's SBG resolver answers with the backend address (1149)",
		InstallSbgSessionResolve},
	{"XlspLookup", "xlsp_lookup", ON_TITLE,
		"XLSP lookups of service 584A07DB answer with the backend; its NTP connect is skipped (1149)",
		InstallXlspLookup},
	{"Grid default URL", "gridurl", ON_TITLE,
		"The Grid client's built-in URL points at the backend (1149)", InstallGridUrl},
	{"GRID RSA public keys", "grid_rsa_keys", ON_TITLE,
		"Both Microsoft Grid RSA public moduli are replaced by the LAN key (1317)",
		InstallGridPublicKeys},
};
const PatchTable kBackendRedirectPatches = PATCH_TABLE(kRows);
