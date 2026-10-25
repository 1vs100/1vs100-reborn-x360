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
#include "LauncherLog.h"
#include "../sysdll/platform/Ini.h"
#include "../sysdll/platform/Text.h"
#include "../sysdll/platform/LogConfig.h"

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

#define REVIVAL_INI_FILE "1vs100Reborn.ini"
#define PATCH_SWITCH_FILE "1vs100Reborn.disable"
#define DEFAULT_BACKEND "192.168.42.1"
#define DEFAULT_BACKEND_IN_ADDR 0xC0A82A01u
#define DEFAULT_SBG_PORT 1000
#define DEFAULT_MATCHMAKING_PORT 1001
#define DNS_TIMEOUT_MS 5000
#define REVIVAL_INI_BYTES 4096

/* [log] defaults: file under the HDD root, keeping the last ten. */
#define DEFAULT_LOG_DIR "1vs100Reborn\\logs"
#define DEFAULT_LOG_KEEP 10

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
		Log("Launcher: XexGetModuleHandle(%s) -> %08X handle=%p (DashLaunch not resident?)",
			DASHLAUNCH_MODULE, st, hDashLaunch);
		return FALSE;
	}

	dlaunchStartSysModule_t dlaunchStartSysModule = NULL;
	st = XexGetProcedureAddress(
		hDashLaunch, DLAUNCH_START_SYS_MODULE_ORDINAL, &dlaunchStartSysModule);
	if (!NT_SUCCESS(st) || !dlaunchStartSysModule) {
		Log("Launcher: XexGetProcedureAddress(launch.xex, ord=%u) -> %08X addr=%p",
			DLAUNCH_START_SYS_MODULE_ORDINAL, st, dlaunchStartSysModule);
		return FALSE;
	}

	DWORD loadStatus = dlaunchStartSysModule((char *)path);
	Log("Launcher: dlaunchStartSysModule(%s) -> %08X", path, loadStatus);
	return NT_SUCCESS((NTSTATUS)loadStatus);
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
		Log("Launcher: %s still resident after the unload", SYSDLL_MODULE);
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
		Log("Launcher: XAvatarInitialize -> %08X: no avatar data", hr);
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
		Log("Launcher: XAvatarGetMetadataRandom -> %08X: no avatar data", rc);
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
	Log("Launcher: launch refused: %S", reason);
	XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH,
		(PWCHAR)reason, NULL);
	Sleep(REFUSAL_NOTICE_MS);
	XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
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
		Log("Launcher: XNetStartup -> %d", rc);
		return FALSE;
	}
	rc = WSAStartup(MAKEWORD(2, 2), &wsa);
	if (rc != 0) {
		Log("Launcher: WSAStartup -> %d", rc);
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
		Log("Launcher: XNetDnsLookup(%s) -> %d", host, rc);
	}
	WSACloseEvent(done);
	WSACleanup();
	XNetCleanup();
	return resolved;
}

static VOID SetUint(const IniLine &line, DWORD min, DWORD max, DWORD *out)
{
	if (!Text_ParseUint(line.value, line.valueLen, min, max, out))
		Log("Launcher: config: %.*s is not %u-%u, keeping %u", (int)line.keyLen, line.key, min, max,
			*out);
}

/* log_dir/log_keep are the launcher's own. */
static CHAR g_LogDir[96] = DEFAULT_LOG_DIR;
static DWORD g_LogKeep = DEFAULT_LOG_KEEP;

/* Below the HDD root: no "..", no leading backslash. */
static BOOL IsLogDir(const CHAR *v, DWORD n)
{
	if (n == 0 || n >= sizeof(g_LogDir) || v[0] == '\\')
		return FALSE;
	for (DWORD i = 0; i < n; ++i) {
		CHAR c = v[i];
		BOOL ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '_' || c == '-' || c == '.' || c == ' ' || c == '\\';
		if (!ok || (c == '.' && i + 1 < n && v[i + 1] == '.'))
			return FALSE;
	}
	return TRUE;
}

/* One [log] key; FALSE when not one. A bad value keeps the default. */
static BOOL ReadLogKey(const IniLine &line, RevivalLaunchConfig *config)
{
	const CHAR *v = line.value;
	DWORD n = line.valueLen;
	if (Text_EqualsNoCase(line.key, line.keyLen, "log_sink")) {
		if (!LogConfig_ParseSink(v, n, &config->logSink))
			Log("Launcher: config: log_sink is not debug, file, both or off, keeping the default");
	} else if (Text_EqualsNoCase(line.key, line.keyLen, "log_level")) {
		if (LogConfig_ParseLevel(v, n, FALSE, &config->logLevel))
			config->logLevelSet = TRUE;
		else
			Log("Launcher: config: log_level is not error, warn, info or debug, keeping the "
				"build's default");
	} else if (Text_EqualsNoCase(line.key, line.keyLen, "log_debug_also")) {
		if (!LogConfig_ParseLevel(v, n, TRUE, &config->logDebugAlso))
			Log("Launcher: config: log_debug_also is not error, warn, info, debug or none, "
				"keeping warn");
	} else if (Text_EqualsNoCase(line.key, line.keyLen, "log_buffer_kb")) {
		DWORD value;
		if (Text_ParseUint(v, n, 0, LOG_BUFFER_MAX_KB, &value) && LogConfig_BufferKbValid(value))
			config->logBufferKb = value;
		else
			Log("Launcher: config: log_buffer_kb is not a power of two in %u-%u, keeping %u",
				LOG_BUFFER_MIN_KB, LOG_BUFFER_MAX_KB, config->logBufferKb);
	} else if (Text_EqualsNoCase(line.key, line.keyLen, "log_flush_ms")) {
		SetUint(line, 100, 60000, &config->logFlushMs);
	} else if (Text_EqualsNoCase(line.key, line.keyLen, "log_tags")) {
		if (n < sizeof(config->logTags))
			Text_Append(config->logTags, 0, n + 1, v);
		else
			Log("Launcher: config: log_tags longer than %u characters, ignored",
				(DWORD)sizeof(config->logTags) - 1);
	} else if (Text_EqualsNoCase(line.key, line.keyLen, "log_dir")) {
		while (n > 0 && v[n - 1] == '\\')
			n--;
		if (IsLogDir(v, n))
			Text_Append(g_LogDir, 0, n + 1, v);
		else
			Log("Launcher: config: log_dir is not a plain path below the HDD root, keeping %s",
				g_LogDir);
	} else if (Text_EqualsNoCase(line.key, line.keyLen, "log_keep")) {
		SetUint(line, 1, 1000, &g_LogKeep);
	} else {
		return FALSE;
	}
	return TRUE;
}

/* ini and disable to config. Missing keys keep defaults. Unresolvable host refuses the launch. */
static BOOL ReadRevivalConfig(RevivalLaunchConfig *config)
{
	static CHAR buf[REVIVAL_INI_BYTES];
	CHAR path[MAX_PATH];
	NTSTATUS st;
	IniLine line;
	DWORD pos = 0;
	BOOL isHost = FALSE;

	ZeroMemory(config, sizeof(*config));
	config->size = sizeof(*config);
	config->backendInAddr = DEFAULT_BACKEND_IN_ADDR;
	config->sbgPort = DEFAULT_SBG_PORT;
	config->matchmakingPort = DEFAULT_MATCHMAKING_PORT;
	Text_Append(config->backendHost, 0, sizeof(config->backendHost), DEFAULT_BACKEND);
	config->logSink = LOG_SINK_FILE;
	config->logDebugAlso = LOG_LEVEL_WARN;
	config->logBufferKb = LOG_DEFAULT_BUFFER_KB;
	config->logFlushMs = LOG_DEFAULT_FLUSH_MS;

	if (FindLauncherFile(PATCH_SWITCH_FILE, path, sizeof(path))) {
		LONG got = ReadDeviceFile(path, config->patchSwitches, sizeof(config->patchSwitches), &st);
		config->patchSwitches[got > 0 ? got : 0] = 0;
	}

	LONG n = -1;
	if (FindLauncherFile(REVIVAL_INI_FILE, path, sizeof(path)))
		n = ReadDeviceFile(path, buf, sizeof(buf), &st);
	if (n < 0) {
		Log("Launcher: config: no %s read, using defaults", REVIVAL_INI_FILE);
		n = 0;
	}
	buf[n] = 0;
	while (Ini_Next(buf, (DWORD)n, &pos, &line)) {
		if (Text_EqualsNoCase(line.key, line.keyLen, "address")) {
			DWORD addr;
			if (line.valueLen > REVIVAL_GRID_HOST_MAX) {
				Log("Launcher: config: address longer than %u characters (Grid URL), keeping %s",
					(unsigned)REVIVAL_GRID_HOST_MAX, config->backendHost);
				continue;
			}
			if (Ini_ParseIPv4(line.value, line.valueLen, &addr)) {
				config->backendInAddr = addr;
				isHost = FALSE;
			} else if (Ini_IsHostname(line.value, line.valueLen)) {
				isHost = TRUE;
			} else {
				Log("Launcher: config: address is neither an IPv4 address nor a host name, "
					"keeping %s",
					config->backendHost);
				continue;
			}
			Text_Append(config->backendHost, 0, line.valueLen + 1, line.value);
		} else if (Text_EqualsNoCase(line.key, line.keyLen, "sbg_port")) {
			SetUint(line, 1, 65535, &config->sbgPort);
		} else if (Text_EqualsNoCase(line.key, line.keyLen, "matchmaking_port")) {
			SetUint(line, 1, 65535, &config->matchmakingPort);
		} else if (!ReadLogKey(line, config)) {
			Log("Launcher: config: unknown key '%.*s' ignored", (int)line.keyLen, line.key);
		}
	}
	if (isHost && !ResolveHost(config->backendHost, &config->backendInAddr))
		return FALSE;
	Log("Launcher: config: address=%s (%08X) sbg=%u mm=%u", config->backendHost,
		config->backendInAddr, config->sbgPort, config->matchmakingPort);
	return TRUE;
}

/* Config before the game starts. From then on the launcher logs through it. */
static BOOL HandOverConfig(HANDLE hSysdll, RevivalLaunchConfig *config)
{
	SetRevivalConfig_t setRevivalConfig = NULL;
	NTSTATUS st = XexGetProcedureAddress(hSysdll, REVIVAL_SET_CONFIG_ORDINAL, &setRevivalConfig);
	if (!NT_SUCCESS(st) || !setRevivalConfig) {
		Log("Launcher: XexGetProcedureAddress(%s, ord=%u) -> %08X addr=%p", SYSDLL_MODULE,
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
	/* Config first: a refused launch lands in the file too. */
	RevivalLaunchConfig config;
	BOOL configRead = ReadRevivalConfig(&config);
	LauncherLog_Open(&config, g_LogDir, g_LogKeep);
	const WCHAR *refusal = PreflightRefusal();
	if (refusal) {
		RefuseLaunch(refusal);
		return;
	}
	if (!configRead) {
		RefuseLaunch(L"1 vs 100: the backend host in 1vs100Reborn.ini was not found");
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
	if (!HandOverConfig(hSysdll, &config)) {
		RefuseLaunch(L"1 vs 100: the sysdll did not take its configuration");
		return;
	}
	if (!SetChannelLaunchData()) {
		Log("Launcher: cannot set the launch data, stopping");
		return;
	}
	if (FileExists("game:\\" GAME_XEX))
		LaunchFromDirectory();
	else
		LaunchFromPackage();

	// No launch returns on success.
	Log("Launcher: no game launched");
}
