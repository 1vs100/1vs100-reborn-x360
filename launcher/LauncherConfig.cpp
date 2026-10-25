#include "LauncherConfig.h"
#include "../sysdll/platform/Ini.h"
#include "../sysdll/platform/Text.h"

#define DEFAULT_SBG_PORT 1000
#define DEFAULT_MATCHMAKING_PORT 1001
#define DEFAULT_LOG_DIR "1vs100Reborn\\logs"
#define DEFAULT_LOG_KEEP 10

static BOOL IsKey(const IniLine &line, const CHAR *key)
{
	return Text_EqualsNoCase(line.key, line.keyLen, key);
}

static VOID Invalid(const IniLine &line, const CHAR *expected, ConfigWarn_t warn)
{
	warn("Launcher: config: [%.*s] %.*s=%.*s is not %s, ignored", (int)line.sectionLen,
		line.section, (int)line.keyLen, line.key, (int)line.valueLen, line.value, expected);
}

static VOID TooLong(const IniLine &line, DWORD max, ConfigWarn_t warn)
{
	warn("Launcher: config: [%.*s] %.*s is longer than %u characters, ignored",
		(int)line.sectionLen, line.section, (int)line.keyLen, line.key, max);
}

static VOID SetUint(const IniLine &line, DWORD min, DWORD max, DWORD *out, ConfigWarn_t warn)
{
	if (!Text_ParseUint(line.value, line.valueLen, min, max, out))
		warn("Launcher: config: [%.*s] %.*s=%.*s is not %u-%u, ignored", (int)line.sectionLen,
			line.section, (int)line.keyLen, line.key, (int)line.valueLen, line.value, min, max);
}

/* IPv4 or a host name; the Grid URL bounds the length. */
static BOOL SetHost(const IniLine &line, LauncherConfig *launcher, ConfigWarn_t warn)
{
	RevivalLaunchConfig *config = &launcher->handover;
	DWORD addr = 0;
	BOOL isName;
	if (line.valueLen > REVIVAL_GRID_HOST_MAX) {
		TooLong(line, REVIVAL_GRID_HOST_MAX, warn);
		return FALSE;
	}
	isName = !Ini_ParseIPv4(line.value, line.valueLen, &addr);
	if (isName && !Ini_IsHostname(line.value, line.valueLen)) {
		Invalid(line, "an IPv4 address or a host name", warn);
		return FALSE;
	}
	config->backendInAddr = addr;
	launcher->hostIsName = isName;
	Text_Append(config->backendHost, 0, line.valueLen + 1, line.value);
	return TRUE;
}

/* Below the HDD root: no "..", no leading backslash. */
static BOOL IsLogDir(const CHAR *v, DWORD n)
{
	if (n == 0 || n >= LAUNCHER_LOG_DIR_BYTES || v[0] == '\\')
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

/* FALSE for an unknown key. */
static BOOL SetBackendKey(const IniLine &line, LauncherConfig *launcher, BOOL *hostSet,
	ConfigWarn_t warn)
{
	RevivalLaunchConfig *config = &launcher->handover;
	if (IsKey(line, "host")) {
		if (SetHost(line, launcher, warn))
			*hostSet = TRUE;
	} else if (IsKey(line, "sbg_port")) {
		SetUint(line, 1, 65535, &config->sbgPort, warn);
	} else if (IsKey(line, "matchmaking_port")) {
		SetUint(line, 1, 65535, &config->matchmakingPort, warn);
	} else {
		return FALSE;
	}
	return TRUE;
}

/* FALSE for an unknown key. */
static BOOL SetLogKey(const IniLine &line, LauncherConfig *launcher, ConfigWarn_t warn)
{
	RevivalLaunchConfig *config = &launcher->handover;
	const CHAR *v = line.value;
	DWORD n = line.valueLen;
	if (IsKey(line, "sink")) {
		if (!LogConfig_ParseSink(v, n, &config->logSink))
			Invalid(line, "off, debug, file or both", warn);
	} else if (IsKey(line, "level")) {
		if (LogConfig_ParseLevel(v, n, FALSE, &config->logLevel))
			config->logLevelSet = TRUE;
		else
			Invalid(line, "error, warn, info or debug", warn);
	} else if (IsKey(line, "debug_also")) {
		if (!LogConfig_ParseLevel(v, n, TRUE, &config->logDebugAlso))
			Invalid(line, "error, warn, info, debug or none", warn);
	} else if (IsKey(line, "buffer_kb")) {
		DWORD value;
		if (Text_ParseUint(v, n, 0, LOG_BUFFER_MAX_KB, &value) && LogConfig_BufferKbValid(value))
			config->logBufferKb = value;
		else
			warn("Launcher: config: [log] buffer_kb=%.*s is not a power of two in %u-%u, ignored",
				(int)n, v, LOG_BUFFER_MIN_KB, LOG_BUFFER_MAX_KB);
	} else if (IsKey(line, "flush_ms")) {
		SetUint(line, 100, 60000, &config->logFlushMs, warn);
	} else if (IsKey(line, "tags")) {
		if (n < sizeof(config->logTags))
			Text_Append(config->logTags, 0, n + 1, v);
		else
			TooLong(line, sizeof(config->logTags) - 1, warn);
	} else if (IsKey(line, "dir")) {
		while (n > 0 && v[n - 1] == '\\')
			n--;
		if (IsLogDir(v, n))
			Text_Append(launcher->logDir, 0, n + 1, v);
		else
			Invalid(line, "a plain path below the HDD root", warn);
	} else if (IsKey(line, "keep")) {
		SetUint(line, 1, 1000, &launcher->logKeep, warn);
	} else {
		return FALSE;
	}
	return TRUE;
}

BOOL LauncherConfig_Parse(const CHAR *text, DWORD len, LauncherConfig *launcher, ConfigWarn_t warn)
{
	RevivalLaunchConfig *config = &launcher->handover;
	IniReader reader;
	IniLine line;
	BOOL hostSet = FALSE;

	*launcher = LauncherConfig();
	config->size = sizeof(*config);
	config->sbgPort = DEFAULT_SBG_PORT;
	config->matchmakingPort = DEFAULT_MATCHMAKING_PORT;
	config->logSink = LOG_SINK_FILE;
	config->logDebugAlso = LOG_LEVEL_WARN;
	config->logBufferKb = LOG_DEFAULT_BUFFER_KB;
	config->logFlushMs = LOG_DEFAULT_FLUSH_MS;
	Text_Append(launcher->logDir, 0, sizeof(launcher->logDir), DEFAULT_LOG_DIR);
	launcher->logKeep = DEFAULT_LOG_KEEP;

	Ini_Open(&reader, text, len);
	while (Ini_Next(&reader, &line)) {
		BOOL known = FALSE;
		if (Text_EqualsNoCase(line.section, line.sectionLen, "backend"))
			known = SetBackendKey(line, launcher, &hostSet, warn);
		else if (Text_EqualsNoCase(line.section, line.sectionLen, "log"))
			known = SetLogKey(line, launcher, warn);
		if (!known)
			warn("Launcher: config: unknown key [%.*s] %.*s, ignored", (int)line.sectionLen,
				line.section, (int)line.keyLen, line.key);
	}
	if (!hostSet)
		warn("Launcher: config: no valid [backend] host");
	return hostSet;
}
