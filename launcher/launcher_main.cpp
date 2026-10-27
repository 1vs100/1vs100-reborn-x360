// Loads the sysdll system-wide, hands it the backend config, sets the launch channel, starts the game.
//
// Only way to start the game: the sysdll patches nothing without its config.
//
// Sysdll, ini, switch file: next to the launcher first, then the HDD root.
//
// Both launch paths are soft transitions, so the guide launch data survives.
//
// Pre-flight: user 0 signed in with avatar data, or no launch.
//
// A resident sysdll unloads first. Any failure or unresolvable host refuses the launch.
//
// Logged as "Launcher:"; last lines go through the sysdll into the same file.

#include <xtl.h>
#include <xkelib.h>
#include <xavatar.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../sysdll/config/LaunchHandover.h"
#include "DeviceFile.h"
#include "LauncherConfig.h"
#include "LauncherLog.h"

// The CRT guard covers sysdll title threads; the launcher may use the CRT.
#pragma warning(disable : 4995)

#define DEFAULT_CHANNEL "1vs100"
#define PACKAGE_PATH \
	"\\Device\\Harddisk0\\Partition1\\Content\\0000000000000000\\584A07D1\\00000002\\2D23F392B51E38B1A93FA1E1B9FAE7B2C5DBBD6D58"
#define PACKAGE_XEX "default.xex"

#define GAME_XEX "1vs100-fre.xex"

#define SYSDLL_MODULE "1vs100Reborn.xex"
#define DASHLAUNCH_MODULE "launch.xex"
#define DLAUNCH_START_SYS_MODULE_ORDINAL 2

typedef DWORD (*dlaunchStartSysModule_t)(char *modPath);
#define DLAUNCH_GET_OPT_VAL_BY_NAME_ORDINAL 9
#define DLAUNCH_SET_OPT_VAL_BY_NAME_ORDINAL 10
typedef BOOL (*dlaunchGetOptValByName_t)(char *name, DWORD *value);
typedef BOOL (*dlaunchSetOptValByName_t)(char *name, DWORD *value);

#define REVIVAL_INI_FILE "1vs100Reborn.ini"
#define PATCH_SWITCH_FILE "1vs100Reborn.disable"
#define DNS_TIMEOUT_MS 5000
#define REVIVAL_INI_BYTES 4096

#define AVATAR_METADATA_TIMEOUT_MS 10000
#define REFUSAL_NOTICE_MS 3000

static BOOL FileExists(const CHAR *path)
{
	DWORD attrs = GetFileAttributesA(path);
	return attrs != 0xFFFFFFFFu && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

/* game:\ target, no trailing backslash; empty when unreadable. */
static CHAR g_GameDir[256];

static VOID ResolveGameDir()
{
	ANSI_STRING name;
	OBJECT_ATTRIBUTES attrs;
	STRING target;
	HANDLE link = NULL;
	NTSTATUS st;

	g_GameDir[0] = 0;
	RtlInitAnsiString(&name, "\\??\\game:");
	InitializeObjectAttributes(&attrs, &name, OBJ_CASE_INSENSITIVE, NULL);
	st = NtOpenSymbolicLinkObject(&link, &attrs);
	if (NT_SUCCESS(st)) {
		target.Buffer = g_GameDir;
		target.Length = 0;
		target.MaximumLength = sizeof(g_GameDir) - 1;
		st = NtQuerySymbolicLinkObject(link, &target, NULL);
		NtClose(link);
		if (NT_SUCCESS(st)) {
			DWORD n = target.Length;
			while (n > 0 && g_GameDir[n - 1] == '\\')
				n--;
			g_GameDir[n] = 0;
		} else {
			g_GameDir[0] = 0;
		}
	}
	if (g_GameDir[0])
		Log("Launcher: game:\\ is %s", g_GameDir);
	else
		Log("Launcher: game:\\ not resolved (%08X), using the HDD root only", st);
}

/* Next to the launcher, else the HDD root. FALSE when neither. */
static BOOL FindLauncherFile(const CHAR *name, CHAR *out, DWORD cap)
{
	if (g_GameDir[0]) {
		_snprintf(out, cap - 1, "%s\\%s", g_GameDir, name);
		out[cap - 1] = 0;
		if (DeviceFileExists(out)) {
			Log("Launcher: %s found next to the launcher: %s", name, out);
			return TRUE;
		}
	}
	_snprintf(out, cap - 1, "%s\\%s", HDD_ROOT, name);
	out[cap - 1] = 0;
	if (DeviceFileExists(out)) {
		Log("Launcher: %s found on the HDD root: %s", name, out);
		return TRUE;
	}
	Log("Launcher: %s found neither next to the launcher nor on the HDD root", name);
	return FALSE;
}

static BOOL LoadSysdllViaDashLaunch(const CHAR *path)
{
	HANDLE hDashLaunch = NULL;
	NTSTATUS st = XexGetModuleHandle((PSZ)DASHLAUNCH_MODULE, &hDashLaunch);
	if (!NT_SUCCESS(st) || !hDashLaunch) {
		LogWarn("Launcher: XexGetModuleHandle(%s) -> %08X handle=%p (DashLaunch not resident?)",
			DASHLAUNCH_MODULE, st, hDashLaunch);
		return FALSE;
	}

	dlaunchStartSysModule_t dlaunchStartSysModule = NULL;
	st = XexGetProcedureAddress(
		hDashLaunch, DLAUNCH_START_SYS_MODULE_ORDINAL, &dlaunchStartSysModule);
	if (!NT_SUCCESS(st) || !dlaunchStartSysModule) {
		LogWarn("Launcher: XexGetProcedureAddress(launch.xex, ord=%u) -> %08X addr=%p",
			DLAUNCH_START_SYS_MODULE_ORDINAL, st, dlaunchStartSysModule);
		return FALSE;
	}

	DWORD loadStatus = dlaunchStartSysModule((char *)path);
	Log("Launcher: dlaunchStartSysModule(%s) -> %08X", path, loadStatus);
	return NT_SUCCESS((NTSTATUS)loadStatus);
}

/* DashLaunch options the backend traffic relies on: sockpatch gives every title the insecure
   socket privilege, xhttp lets XAM's HTTP stack talk to insecure hosts. Switched on at run time,
   so a launch.ini that lacks them does not matter. A failure is logged and does not stop the
   launch. */
static const char *const kDashLaunchOptions[] = {"sockpatch", "xhttp"};

static VOID EnsureDashLaunchOptions()
{
	HANDLE hDashLaunch = NULL;
	dlaunchGetOptValByName_t getOpt = NULL;
	dlaunchSetOptValByName_t setOpt = NULL;

	if (!NT_SUCCESS(XexGetModuleHandle((PSZ)DASHLAUNCH_MODULE, &hDashLaunch)) || !hDashLaunch) {
		LogWarn("Launcher: DashLaunch options: DashLaunch not resident");
		return;
	}
	XexGetProcedureAddress(hDashLaunch, DLAUNCH_GET_OPT_VAL_BY_NAME_ORDINAL, &getOpt);
	XexGetProcedureAddress(hDashLaunch, DLAUNCH_SET_OPT_VAL_BY_NAME_ORDINAL, &setOpt);
	if (!getOpt || !setOpt) {
		LogWarn("Launcher: DashLaunch options: no getopt/setopt (%p %p)", getOpt, setOpt);
		return;
	}
	for (DWORD i = 0; i < sizeof(kDashLaunchOptions) / sizeof(kDashLaunchOptions[0]); ++i) {
		char *name = (char *)kDashLaunchOptions[i];
		DWORD value = 0;
		BOOL got = getOpt(name, &value);
		if (got && value != 0) {
			Log("Launcher: DashLaunch %s: on", name);
			continue;
		}
		value = 1;
		BOOL set = setOpt(name, &value);
		value = 0;
		got = getOpt(name, &value);
		if (got && value != 0)
			Log("Launcher: DashLaunch %s: was off, switched on", name);
		else
			LogWarn("Launcher: DashLaunch %s: still off (set -> %d, get -> %d)", name, set, got);
	}
}

static HANDLE ResidentModule(const CHAR *name)
{
	HANDLE hModule = NULL;
	NTSTATUS st = XexGetModuleHandle((PSZ)name, &hModule);
	Log("Launcher: XexGetModuleHandle(%s) -> %08X handle=%p", name, st, hModule);
	return NT_SUCCESS(st) ? hModule : NULL;
}

/* Load count set to 1 first, so one unload takes it down. */
static BOOL UnloadResidentSysdll()
{
	HANDLE hSysdll = ResidentModule(SYSDLL_MODULE);
	if (!hSysdll)
		return TRUE;
	PLDR_DATA_TABLE_ENTRY ldr = (PLDR_DATA_TABLE_ENTRY)hSysdll;
	Log("Launcher: %s resident (load count %u), unloading", SYSDLL_MODULE, ldr->LoadCount);
	ldr->LoadCount = 1;
	XexUnloadImage(hSysdll);
	if (ResidentModule(SYSDLL_MODULE)) {
		LogWarn("Launcher: %s still resident after the unload", SYSDLL_MODULE);
		return FALSE;
	}
	Log("Launcher: %s unloaded", SYSDLL_MODULE);
	return TRUE;
}

static BOOL ProfileSignedIn()
{
	XUSER_SIGNIN_STATE state = XUserGetSigninState(0);
	Log("Launcher: user 0 sign-in state %u", (DWORD)state);
	return state != eXUserSigninState_NotSignedIn;
}

/* No avatar data: the game dies on a Lua error after sign-in. Any failure refuses the launch. */
static BOOL AvatarDataInstalled()
{
	static XAVATAR_METADATA metadata;
	HRESULT hr = XAvatarInitialize(XAVATAR_COORDINATE_SYSTEM_RIGHT_HANDED, 0,
		XAVATAR_INITIALIZE_DEFAULT_ASSET_LOAD_THREAD, 2, NULL);
	if (FAILED(hr)) {
		LogWarn("Launcher: XAvatarInitialize -> %08X: no avatar data", hr);
		return FALSE;
	}

	XOVERLAPPED ov;
	ZeroMemory(&ov, sizeof(ov));
	ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	DWORD rc = ov.hEvent ? XAvatarGetMetadataRandom(XAVATAR_BODY_TYPE_ALL, 1, &metadata, &ov)
						 : GetLastError();
	if (rc == ERROR_IO_PENDING) {
		if (WaitForSingleObject(ov.hEvent, AVATAR_METADATA_TIMEOUT_MS) == WAIT_OBJECT_0) {
			rc = XGetOverlappedResult(&ov, NULL, FALSE);
		} else {
			XCancelOverlapped(&ov);
			rc = ERROR_TIMEOUT;
		}
	}
	if (ov.hEvent)
		CloseHandle(ov.hEvent);
	XAvatarShutdown();

	if (rc != ERROR_SUCCESS) {
		LogWarn("Launcher: XAvatarGetMetadataRandom -> %08X: no avatar data", rc);
		return FALSE;
	}
	Log("Launcher: avatar data present");
	return TRUE;
}

/* NULL when ready. */
static const WCHAR *PreflightRefusal()
{
	BOOL signedIn = ProfileSignedIn();
	BOOL avatars = AvatarDataInstalled();

	if (!signedIn)
		return L"1 vs 100: sign in a profile first";
	if (!avatars)
		return L"1 vs 100: avatar data is not installed on this console";
	return NULL;
}

/* Notify, then back to the default app. */
static VOID RefuseLaunch(const WCHAR *reason)
{
	LogError("Launcher: launch refused: %S", reason);
	XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH,
		(PWCHAR)reason, NULL);
	Sleep(REFUSAL_NOTICE_MS);
	XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
}

#define PROBE_PORT 80
/* Fixed path: the server operator sees consoles check in. */
#define PROBE_PATH "/hi_x360"
#define PROBE_TIMEOUT_MS 3000

/* One HTTP GET of PROBE_PATH to the backend on the launcher's own stack: one log line saying
   whether the host answers. Insecure only, never SO_PRIVATE (that confines a socket to the
   subnet). Never stops the launch. */
static VOID ProbeBackend(const CHAR *host, DWORD inAddr)
{
	BOOL on = TRUE;
	DWORD timeout = PROBE_TIMEOUT_MS;
	SOCKADDR_IN addr;
	CHAR request[160];
	CHAR reply[128];
	INT rc, len;

	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) {
		LogWarn("Launcher: backend %s:%u: socket -> %d", host, PROBE_PORT, WSAGetLastError());
		return;
	}
	setsockopt(s, SOL_SOCKET, SO_MARKINSECURE, (const CHAR *)&on, sizeof(on));
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const CHAR *)&timeout, sizeof(timeout));
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const CHAR *)&timeout, sizeof(timeout));

	len = _snprintf(request, sizeof(request) - 1,
		"GET " PROBE_PATH " HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", host);
	if (len < 0) {
		LogWarn("Launcher: backend %s: host name too long for the probe", host);
		closesocket(s);
		return;
	}

	ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.S_un.S_addr = inAddr;
	addr.sin_port = htons(PROBE_PORT);
	/* The socket timeouts do not bound connect(): connect non-blocking, wait in select(). */
	u_long nonBlocking = 1;
	ioctlsocket(s, FIONBIO, &nonBlocking);
	if (connect(s, (SOCKADDR *)&addr, sizeof(addr)) != 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
		LogWarn("Launcher: backend %s:%u: connect -> %d", host, PROBE_PORT, WSAGetLastError());
		closesocket(s);
		return;
	}
	fd_set writable, failed;
	FD_ZERO(&writable);
	FD_ZERO(&failed);
	FD_SET(s, &writable);
	FD_SET(s, &failed);
	TIMEVAL wait = {PROBE_TIMEOUT_MS / 1000, (PROBE_TIMEOUT_MS % 1000) * 1000};
	rc = select(0, NULL, &writable, &failed, &wait);
	if (rc <= 0 || !FD_ISSET(s, &writable)) {
		LogWarn("Launcher: backend %s:%u: connect %s (select -> %d, error %d)", host, PROBE_PORT,
			rc == 0 ? "timed out" : "failed", rc, WSAGetLastError());
		closesocket(s);
		return;
	}
	nonBlocking = 0;
	ioctlsocket(s, FIONBIO, &nonBlocking);
	rc = send(s, request, len, 0);
	if (rc != len) {
		LogWarn("Launcher: backend %s:%u: send -> %d, error %d", host, PROBE_PORT, rc,
			WSAGetLastError());
		closesocket(s);
		return;
	}
	rc = recv(s, reply, sizeof(reply) - 1, 0);
	if (rc <= 0) {
		LogWarn("Launcher: backend %s:%u: recv -> %d, error %d", host, PROBE_PORT, rc,
			WSAGetLastError());
	} else {
		reply[rc] = 0;
		for (len = 0; len < rc && reply[len] != '\r' && reply[len] != '\n'; ++len) {
		}
		reply[len] = 0;
		Log("Launcher: backend %s:%u%s reachable: \"%s\"", host, PROBE_PORT, PROBE_PATH, reply);
	}
	closesocket(s);
}

/* Hostname to IPv4 through the console DNS. Starts and stops its own stack. */
static BOOL ResolveHost(const CHAR *host, DWORD *inAddr)
{
	XNetStartupParams xnsp;
	WSADATA wsa;
	XNDNS *dns = NULL;
	BOOL resolved = FALSE;

	ZeroMemory(&xnsp, sizeof(xnsp));
	xnsp.cfgSizeOfStruct = sizeof(xnsp);
	xnsp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
	INT rc = XNetStartup(&xnsp);
	if (rc != 0) {
		LogWarn("Launcher: XNetStartup -> %d", rc);
		return FALSE;
	}
	rc = WSAStartup(MAKEWORD(2, 2), &wsa);
	if (rc != 0) {
		LogWarn("Launcher: WSAStartup -> %d", rc);
		XNetCleanup();
		return FALSE;
	}
	WSAEVENT done = WSACreateEvent();
	rc = XNetDnsLookup(host, done, &dns);
	if (rc == 0) {
		WaitForSingleObject(done, DNS_TIMEOUT_MS);
		if (dns->iStatus == 0 && dns->cina > 0) {
			*inAddr = dns->aina[0].S_un.S_addr;
			resolved = TRUE;
		}
		Log("Launcher: DNS %s -> status %d, %u address(es)", host, dns->iStatus, dns->cina);
		XNetDnsRelease(dns);
	} else {
		LogWarn("Launcher: XNetDnsLookup(%s) -> %d", host, rc);
	}
	if (resolved)
		ProbeBackend(host, *inAddr);
	WSACloseEvent(done);
	WSACleanup();
	XNetCleanup();
	return resolved;
}

/* -1 when unreadable or when the file fills buf, so its end would be lost. */
static LONG ReadConfigFile(const CHAR *path, CHAR *buf, DWORD cap)
{
	NTSTATUS st;
	LONG got = ReadDeviceFile(path, buf, cap, &st);
	if (got < 0)
		LogWarn("Launcher: config: cannot read %s (%08X)", path, st);
	else if ((DWORD)got >= cap - 1)
		LogWarn("Launcher: config: %s is longer than %u bytes", path, cap - 2);
	else
		return got;
	return -1;
}

/* ini and disable to config. NULL when the launch may go ahead. */
static const WCHAR *ReadRevivalConfig(LauncherConfig *config)
{
	static CHAR buf[REVIVAL_INI_BYTES];
	RevivalLaunchConfig *handover = &config->handover;
	CHAR path[MAX_PATH];
	LONG n = -1;

	if (FindLauncherFile(REVIVAL_INI_FILE, path, sizeof(path)))
		n = ReadConfigFile(path, buf, sizeof(buf));
	/* Parsed even without a file: the log needs its settings. */
	BOOL valid = LauncherConfig_Parse(buf, n > 0 ? (DWORD)n : 0, config, LogWarn);
	if (n < 0)
		valid = FALSE;

	if (FindLauncherFile(PATCH_SWITCH_FILE, path, sizeof(path))) {
		LONG got = ReadConfigFile(path, handover->patchSwitches, sizeof(handover->patchSwitches));
		if (got < 0)
			return L"1 vs 100: 1vs100Reborn.disable is unreadable or too long, see the log";
		handover->patchSwitches[got] = 0;
	}

	if (!valid)
		return L"1 vs 100: 1vs100Reborn.ini is incomplete or invalid, see the log";
	if (config->hostIsName && !ResolveHost(handover->backendHost, &handover->backendInAddr))
		return L"1 vs 100: the backend host in 1vs100Reborn.ini did not resolve";
	Log("Launcher: config: host=%s (%08X) sbg=%u mm=%u mads=%u", handover->backendHost,
		handover->backendInAddr, handover->sbgPort, handover->matchmakingPort, handover->madsPort);
	return NULL;
}

/* Config before the game starts. From then on the launcher logs through it. */
static BOOL HandOverConfig(HANDLE hSysdll, RevivalLaunchConfig *config)
{
	SetRevivalConfig_t setRevivalConfig = NULL;
	NTSTATUS st = XexGetProcedureAddress(hSysdll, REVIVAL_SET_CONFIG_ORDINAL, &setRevivalConfig);
	if (!NT_SUCCESS(st) || !setRevivalConfig) {
		LogWarn("Launcher: XexGetProcedureAddress(%s, ord=%u) -> %08X addr=%p", SYSDLL_MODULE,
			REVIVAL_SET_CONFIG_ORDINAL, st, setRevivalConfig);
		return FALSE;
	}
	config->logFileBytes = LauncherLog_FileBytes();
	BOOL taken = setRevivalConfig(config);
	if (taken) {
		RevivalLogLine_t logLine = NULL;
		st = XexGetProcedureAddress(hSysdll, REVIVAL_LOG_LINE_ORDINAL, &logLine);
		if (NT_SUCCESS(st) && logLine)
			LauncherLog_ThroughSysdll(logLine);
	}
	Log("Launcher: SetRevivalConfig -> %d", taken);
	return taken;
}

static BOOL SetChannelLaunchData()
{
	struct {
		DWORD type;
		WCHAR channel[32];
	} blob;
	C_ASSERT(sizeof(blob) == 68);
	const CHAR *channel = DEFAULT_CHANNEL;

	ZeroMemory(&blob, sizeof(blob));
	blob.type = 1;
	for (DWORD i = 0; channel[i] && i < 31; ++i)
		blob.channel[i] = (WCHAR)(BYTE)channel[i];
	NTSTATUS st = XamLoaderSetLaunchData(&blob, sizeof(blob));
	Log("Launcher: XamLoaderSetLaunchData(channel=%s) -> %08X", channel, st);
	return NT_SUCCESS(st);
}

/* Bare file name: resolved against the running title directory. */
static VOID LaunchFromDirectory()
{
	Log("Launcher: directory mode: launching %s", GAME_XEX);
	XamLoaderLaunchTitle(GAME_XEX, 0);
	Log("Launcher: XamLoaderLaunchTitle returned (launch refused?)");
}

/* Never returns on success */
static VOID LaunchFromPackage()
{
	Log("Launcher: package %s image %s", PACKAGE_PATH, PACKAGE_XEX);
	DWORD rc = XamContentLaunchImageFromFileInternal(PACKAGE_PATH, PACKAGE_XEX);
	Log("Launcher: XamContentLaunchImageFromFileInternal -> %08X", rc);
}

void __cdecl main()
{
	Log("Launcher: starting");
	ResolveGameDir();
	/* Before the backend probe in the config read: its insecure socket needs sockpatch. */
	EnsureDashLaunchOptions();
	/* Config first: a refused launch lands in the file too. */
	LauncherConfig config;
	const WCHAR *configRefusal = ReadRevivalConfig(&config);
	LauncherLog_Open(&config.handover, config.logDir, config.logKeep);
	const WCHAR *refusal = PreflightRefusal();
	if (!refusal)
		refusal = configRefusal;
	if (refusal) {
		RefuseLaunch(refusal);
		return;
	}
	if (!UnloadResidentSysdll()) {
		RefuseLaunch(L"1 vs 100: the running sysdll could not be unloaded");
		return;
	}
	CHAR sysdllPath[MAX_PATH];
	if (!FindLauncherFile(SYSDLL_MODULE, sysdllPath, sizeof(sysdllPath))) {
		RefuseLaunch(L"1 vs 100: 1vs100Reborn.xex not found (next to the launcher or on the HDD)");
		return;
	}
	HANDLE hSysdll = NULL;
	if (!LoadSysdllViaDashLaunch(sysdllPath) || (hSysdll = ResidentModule(SYSDLL_MODULE)) == NULL) {
		RefuseLaunch(L"1 vs 100: the sysdll could not be loaded (DashLaunch?)");
		return;
	}
	if (!HandOverConfig(hSysdll, &config.handover)) {
		RefuseLaunch(L"1 vs 100: the sysdll did not take its configuration");
		return;
	}
	if (!SetChannelLaunchData()) {
		LogError("Launcher: cannot set the launch data, stopping");
		return;
	}
	if (FileExists("game:\\" GAME_XEX))
		LaunchFromDirectory();
	else
		LaunchFromPackage();

	// No launch returns on success.
	LogError("Launcher: no game launched");
}
