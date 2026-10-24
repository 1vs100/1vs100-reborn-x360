/* Service-discovery logging. Observational only. */
#include "DiagnosticTables.h"
#include "../registry/PatchHelpers.h"
#include "../platform/ImportHook.h"
#include "../platform/Log.h"

extern "C" void *_ReturnAddress(void);
#pragma intrinsic(_ReturnAddress)

static LogSeenSet g_SeenDestinations;

static VOID LogDestination(const char *api, const struct sockaddr *to)
{
	const struct sockaddr_in *in = (const struct sockaddr_in *)to;
	if (!to || in->sin_family != AF_INET)
		return;
	/* Low 16 bits of address + port; collisions only hide a line. */
	if (Log_FirstSeen(
			&g_SeenDestinations, 0x50000000u ^ (in->sin_addr.S_un.S_addr << 16) ^ in->sin_port)) {
		LOG_DEBUG("netlog", "%s first to %u.%u.%u.%u:%u", api,
			(in->sin_addr.S_un.S_addr >> 24) & 0xFF, (in->sin_addr.S_un.S_addr >> 16) & 0xFF,
			(in->sin_addr.S_un.S_addr >> 8) & 0xFF, in->sin_addr.S_un.S_addr & 0xFF, in->sin_port);
	}
}

static INT NetDll_XNetDnsLookupHook(XNCALLER_TYPE n, LPCSTR host, WSAEVENT ev, XNDNS **out)
{
	INT rc = NetDll_XNetDnsLookup(n, host, ev, out);
	LOG_DEBUG("netlog", "XNetDnsLookup host=%s rc=%d", host ? host : "(null)", rc);
	return rc;
}

static INT NetDll_XNetConnectHook(XNCALLER_TYPE n, const IN_ADDR ina)
{
	INT rc = NetDll_XNetConnect(n, ina);
	LOG_DEBUG("netlog", "XNetConnect ina=%08X rc=%d", ina.S_un.S_addr, rc);
	return rc;
}

static INT NetDll_connectHook(XNCALLER_TYPE n, SOCKET sock, const struct sockaddr *name, int len)
{
	LogDestination("connect", name);
	return NetDll_connect(n, sock, name, len);
}

static INT NetDll_sendtoHook(XNCALLER_TYPE n, SOCKET sock, const VOID *buf, int len, int flags,
	const struct sockaddr *to, int tolen)
{
	LogDestination("sendto", to);
	return NetDll_sendto(n, sock, buf, len, flags, (struct sockaddr *)to, tolen);
}

static INT NetDll_WSASendToHook(XNCALLER_TYPE n, SOCKET sock, LPWSABUF bufs, DWORD count,
	LPDWORD sent, DWORD flags, const struct sockaddr *to, int tolen, LPWSAOVERLAPPED ov,
	LPWSAOVERLAPPED_COMPLETION_ROUTINE done)
{
	LogDestination("WSASendTo", to);
	return NetDll_WSASendTo(n, sock, bufs, count, sent, flags, to, tolen, ov, done);
}

/* Grid client uses XAM XHttp: log the XHttp calls and status callbacks. */
typedef unsigned __int64 NU64;
typedef NU64 (*NFn8)(NU64, NU64, NU64, NU64, NU64, NU64, NU64, NU64);

static NFn8 XamOrdinal(DWORD ordinal)
{
	return (NFn8)ResolveFunction("xam.xex", ordinal);
}

#define XHTTP_LOG_FN(ident, ordinal, fmt, ...)                                              \
	static NU64 ident##Hook(NU64 a, NU64 b, NU64 c, NU64 d, NU64 e, NU64 f, NU64 g, NU64 h) \
	{                                                                                       \
		static NFn8 orig = NULL;                                                            \
		NU64 rc;                                                                            \
		if (!orig)                                                                          \
			orig = XamOrdinal(ordinal);                                                     \
		rc = orig ? orig(a, b, c, d, e, f, g, h) : 0;                                       \
		LOG_DEBUG("netlog", fmt " -> %08X err=%u", __VA_ARGS__, (DWORD)rc,                  \
			(DWORD)rc ? 0u : GetLastError());                                               \
		return rc;                                                                          \
	}
#define NSTR(x) ((DWORD)(x) ? (const char *)(DWORD)(x) : "(null)")

XHTTP_LOG_FN(XHttpStartup, 201, "XHttpStartup flags=%08X", (DWORD)b)
XHTTP_LOG_FN(XHttpOpen, 203, "XHttpOpen agent=%s access=%u flags=%08X", NSTR(b), (DWORD)c, (DWORD)f)
XHTTP_LOG_FN(XHttpConnect, 205, "XHttpConnect host=%s port=%u flags=%08X", NSTR(c),
	(DWORD)d & 0xFFFF, (DWORD)e)
XHTTP_LOG_FN(XHttpOpenRequest, 207, "XHttpOpenRequest %s %s ver=%s flags=%08X", NSTR(c), NSTR(d),
	NSTR(e), (DWORD)h)
XHTTP_LOG_FN(XHttpSendRequest, 209, "XHttpSendRequest h=%08X hdrlen=%u bodylen=%u", (DWORD)b,
	(DWORD)d, (DWORD)f)
XHTTP_LOG_FN(XHttpReceiveResponse, 210, "XHttpReceiveResponse h=%08X", (DWORD)b)
XHTTP_LOG_FN(XHttpReadData, 212, "XHttpReadData h=%08X len=%u", (DWORD)b, (DWORD)d)
XHTTP_LOG_FN(XHttpQueryHeaders, 211, "XHttpQueryHeaders h=%08X level=%08X name=%s", (DWORD)b,
	(DWORD)c, ((DWORD)c & 0xFFFF) == 0xFFFF /* XHTTP_QUERY_CUSTOM */ ? NSTR(d) : "-")

/* No prototype in this XDK; takes and forwards all ten. */
typedef NU64 (*NFn10)(NU64, NU64, NU64, NU64, NU64, NU64, NU64, NU64, NU64, NU64);
static NU64 XHttpOpenRequestUsingMemoryHook(
	NU64 a, NU64 b, NU64 c, NU64 d, NU64 e, NU64 f, NU64 g, NU64 h, NU64 i, NU64 j)
{
	static NFn10 orig = NULL;
	NU64 rc;
	if (!orig)
		orig = (NFn10)XamOrdinal(208);
	rc = orig ? orig(a, b, c, d, e, f, g, h, i, j) : 0;
	LOG_DEBUG("netlog", "XHttpOpenRequestUsingMemory %s %s ver=%s flags=%08X buf=%u -> %08X err=%u",
		NSTR(c), NSTR(d), NSTR(e), (DWORD)h, (DWORD)j, (DWORD)rc, (DWORD)rc ? 0u : GetLastError());
	return rc;
}

typedef VOID(CALLBACK *XHttpStatusCb)(HINTERNET, DWORD_PTR, DWORD, PVOID, DWORD);
static XHttpStatusCb g_XHttpAppCallback = NULL;

/* Chained callback belongs to the installing title. */
static VOID ResetXHttpCallback()
{
	g_XHttpAppCallback = NULL;
}

static VOID CALLBACK XHttpStatusLogCb(
	HINTERNET h, DWORD_PTR ctx, DWORD status, PVOID info, DWORD len)
{
	if (status == 0x00200000 /* XHTTP_CALLBACK_STATUS_REQUEST_ERROR */ && info && len >= 8) {
		LOG_DEBUG("netlog", "XHttp callback h=%08X REQUEST_ERROR api=%u error=%u", (DWORD)h,
			((DWORD *)info)[0], ((DWORD *)info)[1]);
	} else {
		LOG_DEBUG("netlog", "XHttp callback h=%08X status=%08X info=%08X len=%u", (DWORD)h, status,
			(info && len >= 4) ? *(DWORD *)info : 0, len);
	}
	if (g_XHttpAppCallback)
		g_XHttpAppCallback(h, ctx, status, info, len);
}

/* Title callback chains behind the logger. */
static NU64 XHttpSetStatusCallbackHook(
	NU64 a, NU64 b, NU64 c, NU64 d, NU64 e, NU64 f, NU64 g, NU64 h)
{
	static NFn8 orig = NULL;
	NU64 rc;
	if (!orig)
		orig = XamOrdinal(206);
	if ((DWORD)c && (DWORD)c != (DWORD)XHttpStatusLogCb) {
		g_XHttpAppCallback = (XHttpStatusCb)(DWORD)c;
		c = (NU64)(DWORD)XHttpStatusLogCb;
	}
	rc = orig ? orig(a, b, c, d, e, f, g, h) : 0;
	LOG_DEBUG("netlog", "XHttpSetStatusCallback h=%08X flags=%08X -> %08X", (DWORD)b, (DWORD)d,
		(DWORD)rc);
	return rc;
}

#define TRACE_IMPORT(id, switchName, ordinal, hook, summary) \
	{id, switchName, ON_TITLE, summary, InstallXamImport, NULL, ordinal, (PVOID)(hook)}

static const PatchDescriptor kRows[] = {
	TRACE_IMPORT(
		"NetDll_XNetDnsLookup", "trace_dns", 67, NetDll_XNetDnsLookupHook, "Logs every DNS lookup"),
	TRACE_IMPORT("NetDll_XNetConnect", "trace_xnetconnect", 65, NetDll_XNetConnectHook,
		"Logs every XNetConnect"),
	TRACE_IMPORT("NetDll_connect", "trace_connect", 12, NetDll_connectHook,
		"Logs each new connect destination"),
	TRACE_IMPORT(
		"NetDll_sendto", "trace_sendto", 24, NetDll_sendtoHook, "Logs each new sendto destination"),
	TRACE_IMPORT("NetDll_WSASendTo", "trace_wsasendto", 25, NetDll_WSASendToHook,
		"Logs each new WSASendTo destination"),
	TRACE_IMPORT(
		"NetDll_XHttpStartup", "trace_xhttp_startup", 201, XHttpStartupHook, "Logs XHttpStartup"),
	TRACE_IMPORT("NetDll_XHttpOpen", "trace_xhttp_open", 203, XHttpOpenHook, "Logs XHttpOpen"),
	TRACE_IMPORT(
		"NetDll_XHttpConnect", "trace_xhttp_connect", 205, XHttpConnectHook, "Logs XHttpConnect"),
	{"NetDll_XHttpSetStatusCallback", "trace_xhttp_callback", ON_TITLE,
		"Logs every XHttp status callback", InstallXamImport,
		ResetXHttpCallback, 206, (PVOID)XHttpSetStatusCallbackHook},
	TRACE_IMPORT("NetDll_XHttpOpenRequest", "trace_xhttp_request", 207, XHttpOpenRequestHook,
		"Logs XHttpOpenRequest"),
	TRACE_IMPORT("NetDll_XHttpOpenRequestUsingMemory", "trace_xhttp_request_mem", 208,
		XHttpOpenRequestUsingMemoryHook, "Logs XHttpOpenRequestUsingMemory"),
	TRACE_IMPORT("NetDll_XHttpSendRequest", "trace_xhttp_send", 209, XHttpSendRequestHook,
		"Logs XHttpSendRequest"),
	TRACE_IMPORT("NetDll_XHttpReceiveResponse", "trace_xhttp_receive", 210,
		XHttpReceiveResponseHook, "Logs XHttpReceiveResponse"),
	TRACE_IMPORT("NetDll_XHttpQueryHeaders", "trace_xhttp_headers", 211, XHttpQueryHeadersHook,
		"Logs XHttpQueryHeaders"),
	TRACE_IMPORT(
		"NetDll_XHttpReadData", "trace_xhttp_read", 212, XHttpReadDataHook, "Logs XHttpReadData"),
};
const PatchTable kNetTracePatches = PATCH_TABLE(kRows);
