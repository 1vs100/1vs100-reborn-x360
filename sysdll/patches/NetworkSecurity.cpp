/* Title sockets reach LAN peers and routed hosts: insecure marks, security bypass, backend service address, instant QoS. */
#include "PatchTables.h"
#include "../registry/PatchHelpers.h"
#include "../config/RevivalConfig.h"
#include "../platform/ImportHook.h"
#include "../platform/Log.h"

static XNQOS g_xnqos;
static BYTE g_qosData = 'A';

/* Module imports reach XAM directly; only the title table is rewritten. */
typedef int(NTAPI *NetDll_setsockopt_t)(XNCALLER_TYPE, SOCKET, int, int, const char *, int);

static NetDll_setsockopt_t orig_setsockopt = NULL;

/* Insecure only. SO_PRIVATE would confine the socket to the local subnet: the Grid, ad and
   matchmaking hosts are routed and would get WSAEHOSTUNREACH. */
static VOID MarkInsecureXnc(XNCALLER_TYPE n, SOCKET s)
{
	BOOL b = TRUE;
	NetDll_setsockopt_t setopt;
	if (orig_setsockopt == NULL)
		orig_setsockopt = (NetDll_setsockopt_t)ResolveFunction("xam.xex", 7);
	setopt = orig_setsockopt ? orig_setsockopt : NetDll_setsockopt;
	setopt(n, s, SOL_SOCKET, SO_MARKINSECURE, (char *)&b, sizeof(BOOL));
}

static int NetDll_socketHook(XNCALLER_TYPE n, int af, int type, int protocol)
{
	int s;
	LOG_DEBUG("xam", "socketHook");
	s = NetDll_socket(n, af, type, protocol);
	MarkInsecureXnc(n, s);
	return s;
}

static int NetDll_XNetStartupHook(XNCALLER_TYPE xnc, XNetStartupParams *xnsp)
{
	if (xnsp)
		xnsp->cfgFlags |= XNET_STARTUP_BYPASS_SECURITY;
	return NetDll_XNetStartup(xnc, xnsp);
}

static int NetDll_XNetServerToInAddrHook(
	XNCALLER_TYPE n, IN_ADDR address_in, DWORD title_id, IN_ADDR *address_out)
{
	LOG_DEBUG("netlog", "XNetServerToInAddr in=%08X title=%08X", address_in.S_un.S_addr, title_id);
	(void)n;
	(void)title_id;
	if (address_out)
		address_out->S_un.S_addr = address_in.S_un.S_addr;
	return 0;
}

static int XNetLogonGetServiceInfoHook(
	__in DWORD dwServiceID, __out PXONLINE_SERVICE_INFO pServiceInfo)
{
	LOG_DEBUG("netlog", "XNetLogonGetServiceInfo service=%08X", dwServiceID);
	if (pServiceInfo) {
		pServiceInfo->dwServiceID = dwServiceID;
		pServiceInfo->serviceIP.S_un.S_addr = g_RevivalInAddr;
		pServiceInfo->wServicePort = g_RevivalSbgPort;
		pServiceInfo->wReserved = 0;
	}
	return 0;
}

static VOID CompleteFakeQos(WSAEVENT event, XNQOS **result)
{
	g_xnqos.cxnqos = 1;
	g_xnqos.cxnqosPending = 0;
	g_xnqos.axnqosinfo[0].bFlags = XNET_XNQOSINFO_COMPLETE | XNET_XNQOSINFO_TARGET_CONTACTED;
	g_xnqos.axnqosinfo[0].bReserved = 0;
	g_xnqos.axnqosinfo[0].cProbesXmit = 8;
	g_xnqos.axnqosinfo[0].cProbesRecv = 8;
	g_xnqos.axnqosinfo[0].cbData = 1;
	g_xnqos.axnqosinfo[0].pbData = &g_qosData;
	g_xnqos.axnqosinfo[0].wRttMinInMsecs = 9;
	g_xnqos.axnqosinfo[0].wRttMedInMsecs = 10;
	g_xnqos.axnqosinfo[0].dwUpBitsPerSec = 10 * 1024 * 1024 * 8;
	g_xnqos.axnqosinfo[0].dwDnBitsPerSec = 10 * 1024 * 1024 * 8;
	if (result)
		*result = &g_xnqos;
	if (event)
		NetDll_WSASetEvent(event);
}

static INT NetDll_XNetQosLookupHook(XNCALLER_TYPE n, __in UINT cxna,
	__in_ecount_opt(cxna) const XNADDR *apxna[], __in_ecount_opt(cxna) const XNKID *apxnkid[],
	__in_ecount_opt(cxna) const XNKEY *apxnkey[], __in UINT cina,
	__in_ecount_opt(cina) const IN_ADDR aina[], __in_ecount_opt(cina) const DWORD adwServiceId[],
	__in UINT cProbes, __in DWORD dwBitsPerSec, __in DWORD dwFlags, __in_opt WSAEVENT hEvent,
	__out XNQOS **ppxnqos)
{
	(void)n;
	(void)cxna;
	(void)apxna;
	(void)apxnkid;
	(void)apxnkey;
	(void)cina;
	(void)aina;
	(void)adwServiceId;
	(void)cProbes;
	(void)dwBitsPerSec;
	(void)dwFlags;
	LOG_DEBUG("netlog", "XNetQosLookup cxna=%u cina=%u", cxna, cina);
	CompleteFakeQos(hEvent, ppxnqos);
	return 0;
}

static INT NetDll_XNetQosServiceLookupHook(
	XNCALLER_TYPE n, __in DWORD dwFlags, __in_opt WSAEVENT hEvent, __deref_out XNQOS **ppxnqos)
{
	(void)n;
	(void)dwFlags;

	CompleteFakeQos(hEvent, ppxnqos);
	LOG_DEBUG("netlog", "XNetQosServiceLookup fake complete p=%p", ppxnqos ? *ppxnqos : NULL);
	return 0;
}

static INT NetDll_XNetQosReleaseHook(XNCALLER_TYPE n, __in XNQOS *pxnqos)
{
	LOG_INFO("xam", "NetDll_XNetQosRelease %p", pxnqos);
	if (pxnqos == &g_xnqos)
		return 0;
	return NetDll_XNetQosRelease(n, pxnqos);
}

/* The socket row also runs early, before any game code: the second run reports no_match. */
static const PatchDescriptor kRows[] = {
	{"NetDll_socket", "netdll_socket", ON_EARLY | ON_TITLE,
		"Every title socket is marked insecure (never private: that confines it to the subnet)",
		InstallXamImport, NULL, 3, (PVOID)NetDll_socketHook},
	{"NetDll_XNetStartup", "netdll_xnetstartup", ON_TITLE,
		"XNetStartup gets XNET_STARTUP_BYPASS_SECURITY", InstallXamImport, NULL, 51,
		(PVOID)NetDll_XNetStartupHook},
	{"NetDll_XNetServerToInAddr", "netdll_xnetservertoinaddr", ON_TITLE,
		"XNetServerToInAddr returns the address it was given", InstallXamImport, NULL, 58,
		(PVOID)NetDll_XNetServerToInAddrHook},
	{"NetDll_XNetQosLookup", "netdll_qos_lookup", ON_TITLE,
		"QoS lookups complete at once with fixed good values", InstallXamImport, NULL, 70,
		(PVOID)NetDll_XNetQosLookupHook},
	{"NetDll_XNetQosServiceLookup", "netdll_qos_service_lookup", ON_TITLE,
		"QoS service lookups complete at once with fixed good values", InstallXamImport, NULL, 71,
		(PVOID)NetDll_XNetQosServiceLookupHook},
	{"NetDll_XNetQosRelease", "netdll_qos_release", ON_TITLE,
		"Releasing the fixed QoS result is a no-op", InstallXamImport, NULL, 72,
		(PVOID)NetDll_XNetQosReleaseHook},
	{"XNetLogonGetServiceInfo", "xnetlogon_service_info", ON_TITLE,
		"XOnline service lookups answer with the backend address and SBG port", InstallXamImport,
		NULL, 306, (PVOID)XNetLogonGetServiceInfoHook},
};
const PatchTable kNetworkSecurityPatches = PATCH_TABLE(kRows);
