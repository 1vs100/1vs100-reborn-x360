/* Backend probes, 1149. Observational only. */
#include "DiagnosticTables.h"
#include "../registry/PatchHelpers.h"
#include "../platform/Log.h"

extern "C" void *_ReturnAddress(void);
#pragma intrinsic(_ReturnAddress)

/* Logs what the embedded grid config resolves to. Title string: inline at +4 when capacity < 16, else pointer at +4; length +20, capacity +24. */
typedef int(__fastcall *GridConfigGetter_t)(int a1, int a2);
static GridConfigGetter_t g_GridConfigGetterOriginal = NULL;

static const CHAR *TitleStringData(const DWORD *str)
{
	return str[6] < 0x10u ? (const CHAR *)(str + 1) : (const CHAR *)str[1];
}

static int __fastcall GridConfigGetterHook(int a1, int a2)
{
	int result = g_GridConfigGetterOriginal ? g_GridConfigGetterOriginal(a1, a2) : 0;
	const DWORD *gridServiceUrl = (const DWORD *)a1;
	const DWORD *gameServiceIpAddress = (const DWORD *)(a1 + 28);
	LOG_INFO("net", "grid config: GridServiceUrl=\"%s\" len=%u", TitleStringData(gridServiceUrl),
		gridServiceUrl[5]);
	LOG_INFO("net", "grid config: GameServiceIpAddress=\"%s\" len=%u",
		TitleStringData(gameServiceIpAddress), gameServiceIpAddress[5]);
	return result;
}

static VOID InstallGridConfigGetterLog(const PatchDescriptor *self)
{
	HOOK_SITE(SITE_GRID_CONFIG_GETTER, GridConfigGetterHook, g_GridConfigGetterOriginal, self->id);
}

/* Logs whether the Massive init chain runs at all. */
typedef int(__fastcall *MassiveInitialize_t)(int a1);
static MassiveInitialize_t g_MassiveInitializeOriginal = NULL;

static int __fastcall MassiveInitializeHook(int a1)
{
	int result = g_MassiveInitializeOriginal ? g_MassiveInitializeOriginal(a1) : -1;
	LOG_INFO("net", "massive: Initialize(a1=%08X) -> %d", a1, result);
	return result;
}

typedef int(__fastcall *MassiveDnsThreadStart_t)(int a1, int a2);
static MassiveDnsThreadStart_t g_MassiveDnsThreadStartOriginal = NULL;

static int __fastcall MassiveDnsThreadStartHook(int a1, int a2)
{
	int result = g_MassiveDnsThreadStartOriginal ? g_MassiveDnsThreadStartOriginal(a1, a2) : -1;
	LOG_INFO("net", "massive: DNS thread start(a1=%08X a2=%08X) -> %d", a1, a2, result);
	return result;
}

static VOID InstallMassiveInitializeLog(const PatchDescriptor *self)
{
	HOOK_SITE(
		SITE_MASSIVE_INITIALIZE, MassiveInitializeHook, g_MassiveInitializeOriginal, self->id);
}

static VOID InstallMassiveDnsThreadLog(const PatchDescriptor *self)
{
	HOOK_SITE(SITE_MASSIVE_DNS_THREAD_START, MassiveDnsThreadStartHook,
		g_MassiveDnsThreadStartOriginal, self->id);
}

/* Grid dispatcher steps, 1149. Wrappers pass r3-r10 through and log r3 on return. */
typedef unsigned __int64 U64;
typedef U64 (*Fn8)(U64, U64, U64, U64, U64, U64, U64, U64);

#define GRID_TRACE_FN(ident, label)                                                          \
	static Fn8 g_##ident = NULL;                                                             \
	static U64 ident##Hook(U64 a, U64 b, U64 c, U64 d, U64 e, U64 f, U64 g, U64 h)           \
	{                                                                                        \
		const DWORD caller = (DWORD)_ReturnAddress();                                        \
		U64 rc = g_##ident(a, b, c, d, e, f, g, h);                                          \
		LOG_INFO("net", "[GRIDTRACE] %s rc=%08X a1=%08X a2=%08X a3=%08X caller=%08X", label, \
			(DWORD)rc, (DWORD)a, (DWORD)b, (DWORD)c, caller);                                \
		return rc;                                                                           \
	}

GRID_TRACE_FN(GridClientStart, "sub_82577810 grid client start")
GRID_TRACE_FN(GridClientSetup, "sub_8227CD78 grid url+client setup")
GRID_TRACE_FN(GridUserXuid, "sub_82574118 XamUserGetXUID(user,2)")
GRID_TRACE_FN(GridRequestLoop, "sub_825770B8 grid request loop")
GRID_TRACE_FN(GridShowData, "sub_8227CCA8 GetGridOnlyShowData")
GRID_TRACE_FN(GridResultPost, "sub_8222B198 result post (a2=hr a3=id a4=data)")
GRID_TRACE_FN(GridDecode, "sub_82579930 response decode (Content-Encoding)")
GRID_TRACE_FN(GridInflate, "sub_82579530 gzip inflate (a3=len)")
GRID_TRACE_FN(GridParseXml, "sub_8257C4D8 XML parse (a3=len)")
GRID_TRACE_FN(GridChannelLookup, "sub_825750D8 channel/day lookup (a2=channel a3=index)")

/* Response body handed to the parser; log its start. */
static Fn8 g_GridParse = NULL;
static U64 GridParseHook(U64 a, U64 b, U64 c, U64 d, U64 e, U64 f, U64 g, U64 h)
{
	const BYTE *body = (const BYTE *)(DWORD)b;
	CHAR head[49];
	DWORD i, n = (DWORD)c < 48 ? (DWORD)c : 48;
	U64 rc;
	for (i = 0; i < n && body; i++)
		head[i] = (body[i] >= 0x20 && body[i] < 0x7F) ? (CHAR)body[i] : '.';
	head[i] = 0;
	rc = g_GridParse(a, b, c, d, e, f, g, h);
	LOG_INFO(
		"net", "[GRIDTRACE] sub_82575650 parse rc=%08X len=%u head=%s", (DWORD)rc, (DWORD)c, head);
	return rc;
}

/* Stamps the request with NTP time; only dispatcher calls are logged. */
static Fn8 g_GridNtpTime = NULL;
static U64 GridNtpTimeHook(U64 a, U64 b, U64 c, U64 d, U64 e, U64 f, U64 g, U64 h)
{
	const DWORD caller = (DWORD)_ReturnAddress();
	U64 rc = g_GridNtpTime(a, b, c, d, e, f, g, h);
	if (caller >= 0x82237A18u && caller < 0x82238000u)
		LOG_INFO("net", "[GRIDTRACE] sub_822325F8 ntp time=%08X%08X caller=%08X",
			((DWORD *)(DWORD)a)[0], ((DWORD *)(DWORD)a)[1], caller);
	return rc;
}

struct GridTraceHook {
	SiteId site;
	DWORD hook;
	Fn8 *original;
	const char *name;
};

/* All twelve install together. */
static VOID InstallGridTrace(const PatchDescriptor *self)
{
	static const GridTraceHook hooks[] = {
		{SITE_GRID_TRACE_NTP_TIME, (DWORD)GridNtpTimeHook, &g_GridNtpTime, "GridTraceNtpTime"},
		{SITE_GRID_TRACE_CLIENT_START, (DWORD)GridClientStartHook, &g_GridClientStart,
			"GridTraceClientStart"},
		{SITE_GRID_TRACE_CLIENT_SETUP, (DWORD)GridClientSetupHook, &g_GridClientSetup,
			"GridTraceClientSetup"},
		{SITE_GRID_TRACE_USER_XUID, (DWORD)GridUserXuidHook, &g_GridUserXuid, "GridTraceUserXuid"},
		{SITE_GRID_TRACE_REQUEST_LOOP, (DWORD)GridRequestLoopHook, &g_GridRequestLoop,
			"GridTraceRequestLoop"},
		{SITE_GRID_TRACE_SHOW_DATA, (DWORD)GridShowDataHook, &g_GridShowData, "GridTraceShowData"},
		{SITE_GRID_TRACE_RESULT_POST, (DWORD)GridResultPostHook, &g_GridResultPost,
			"GridTraceResultPost"},
		{SITE_GRID_TRACE_DECODE, (DWORD)GridDecodeHook, &g_GridDecode, "GridTraceDecode"},
		{SITE_GRID_TRACE_INFLATE, (DWORD)GridInflateHook, &g_GridInflate, "GridTraceInflate"},
		{SITE_GRID_TRACE_PARSE, (DWORD)GridParseHook, &g_GridParse, "GridTraceParse"},
		{SITE_GRID_TRACE_PARSE_XML, (DWORD)GridParseXmlHook, &g_GridParseXml, "GridTraceParseXml"},
		{SITE_GRID_TRACE_CHANNEL_LOOKUP, (DWORD)GridChannelLookupHook, &g_GridChannelLookup,
			"GridTraceChannelLookup"},
	};
	DWORD i;

	(void)self;
	for (i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++)
		HookSite(hooks[i].site, hooks[i].hook, (DWORD *)hooks[i].original, hooks[i].name);
}

static const PatchDescriptor kRows[] = {
	{"grid config getter log", "grid_config_log", ON_TITLE,
		"Logs the embedded grid config resolution",
		InstallGridConfigGetterLog},
	{"massive Initialize log", "massive_init_log", ON_TITLE,
		"Logs Massive Initialize and result", InstallMassiveInitializeLog},
	{"massive DNS thread start log", "massive_dns_log", ON_TITLE,
		"Logs the Massive DNS thread start", InstallMassiveDnsThreadLog},
	{"GridTrace", "gridtrace", ON_TITLE,
		"Logs every Grid dispatcher step",
		InstallGridTrace},
};
const PatchTable kBackendProbePatches = PATCH_TABLE(kRows);
