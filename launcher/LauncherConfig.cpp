#include "LauncherConfig.h"
#include "../sysdll/platform/Ini.h"
#include "../sysdll/platform/Text.h"

#define DEFAULT_SBG_PORT 1000
#define DEFAULT_MATCHMAKING_PORT 1001
#define DEFAULT_MADS_PORT 1002
#define DEFAULT_LOG_DIR "1vs100Reborn\\logs"
#define DEFAULT_LOG_KEEP 10

static BOOL IsKey(const IniLine &line, const CHAR *key)
{
	return Text_EqualsNoCase(line.key, line.keyLen, key);
}

/* Each returns FALSE after warn names the problem. */
static BOOL Invalid(const IniLine &line, const CHAR *expected, ConfigWarn_t warn)
{
	warn("Launcher: config: [%.*s] %.*s=%.*s is not %s", (int)line.sectionLen, line.section,
		(int)line.keyLen, line.key, (int)line.valueLen, line.value, expected);
	return FALSE;
}

static BOOL TooLong(const IniLine &line, DWORD max, ConfigWarn_t warn)
{
	warn("Launcher: config: [%.*s] %.*s is longer than %u characters", (int)line.sectionLen,
		line.section, (int)line.keyLen, line.key, max);
	return FALSE;
}

static BOOL Unknown(const IniLine &line, ConfigWarn_t warn)
{
	warn("Launcher: config: unknown key [%.*s] %.*s", (int)line.sectionLen, line.section,
		(int)line.keyLen, line.key);
	return FALSE;
}

static BOOL SetUint(const IniLine &line, DWORD min, DWORD max, DWORD *out, ConfigWarn_t warn)
{
	if (Text_ParseUint(line.value, line.valueLen, min, max, out))
		return TRUE;
	warn("Launcher: config: [%.*s] %.*s=%.*s is not %u-%u", (int)line.sectionLen, line.section,
		(int)line.keyLen, line.key, (int)line.valueLen, line.value, min, max);
	return FALSE;
}

/* IPv4 or a host name; the Grid URL bounds the length. */
static BOOL SetHost(const IniLine &line, LauncherConfig *launcher, ConfigWarn_t warn)
{
	RevivalLaunchConfig *config = &launcher->handover;
	DWORD addr = 0;
	BOOL isName;
	if (line.valueLen > REVIVAL_GRID_HOST_MAX)
		return TooLong(line, REVIVAL_GRID_HOST_MAX, warn);
	isName = !Ini_ParseIPv4(line.value, line.valueLen, &addr);
	if (isName && !Ini_IsHostname(line.value, line.valueLen))
		return Invalid(line, "an IPv4 address or a host name", warn);
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

static BOOL SetBackendKey(const IniLine &line, LauncherConfig *launcher, BOOL *hostSet,
	ConfigWarn_t warn)
{
	RevivalLaunchConfig *config = &launcher->handover;
	if (IsKey(line, "host")) {
		if (!SetHost(line, launcher, warn))
			return FALSE;
		*hostSet = TRUE;
		return TRUE;
	}
	/* Patched into a li immediate; 1149's setter refuses a negative port. */
	if (IsKey(line, "sbg_port"))
		return SetUint(line, 1, REVIVAL_SBG_PORT_MAX, &config->sbgPort, warn);
	if (IsKey(line, "matchmaking_port"))
		return SetUint(line, 1, 65535, &config->matchmakingPort, warn);
	if (IsKey(line, "mads_port"))
		return SetUint(line, 1, 65535, &config->madsPort, warn);
	return Unknown(line, warn);
}

static BOOL SetLogKey(const IniLine &line, LauncherConfig *launcher, ConfigWarn_t warn)
{
	RevivalLaunchConfig *config = &launcher->handover;
	const CHAR *v = line.value;
	DWORD n = line.valueLen;
	if (IsKey(line, "sink")) {
		if (!LogConfig_ParseSink(v, n, &config->logSink))
			return Invalid(line, "off, debug, file or both", warn);
		return TRUE;
	}
	if (IsKey(line, "level")) {
		if (!LogConfig_ParseLevel(v, n, FALSE, &config->logLevel))
			return Invalid(line, "error, warn, info or debug", warn);
		config->logLevelSet = TRUE;
		return TRUE;
	}
	if (IsKey(line, "debug_also")) {
		if (!LogConfig_ParseLevel(v, n, TRUE, &config->logDebugAlso))
			return Invalid(line, "error, warn, info, debug or none", warn);
		return TRUE;
	}
	if (IsKey(line, "buffer_kb")) {
		DWORD value;
		if (Text_ParseUint(v, n, 0, LOG_BUFFER_MAX_KB, &value) && LogConfig_BufferKbValid(value)) {
			config->logBufferKb = value;
			return TRUE;
		}
		warn("Launcher: config: [log] buffer_kb=%.*s is not a power of two in %u-%u", (int)n, v,
			LOG_BUFFER_MIN_KB, LOG_BUFFER_MAX_KB);
		return FALSE;
	}
	if (IsKey(line, "flush_ms"))
		return SetUint(line, 100, 60000, &config->logFlushMs, warn);
	if (IsKey(line, "tags")) {
		if (n >= sizeof(config->logTags))
			return TooLong(line, sizeof(config->logTags) - 1, warn);
		Text_Append(config->logTags, 0, n + 1, v);
		return TRUE;
	}
	if (IsKey(line, "dir")) {
		while (n > 0 && v[n - 1] == '\\')
			n--;
		if (!IsLogDir(v, n))
			return Invalid(line, "a plain path below the HDD root", warn);
		Text_Append(launcher->logDir, 0, n + 1, v);
		return TRUE;
	}
	if (IsKey(line, "keep"))
		return SetUint(line, 1, 1000, &launcher->logKeep, warn);
	return Unknown(line, warn);
}

BOOL LauncherConfig_Parse(const CHAR *text, DWORD len, LauncherConfig *launcher, ConfigWarn_t warn)
{
	RevivalLaunchConfig *config = &launcher->handover;
	IniReader reader;
	IniLine line;
	BOOL hostSet = FALSE, valid = TRUE;

	*launcher = LauncherConfig();
	config->size = sizeof(*config);
	config->sbgPort = DEFAULT_SBG_PORT;
	config->matchmakingPort = DEFAULT_MATCHMAKING_PORT;
	config->madsPort = DEFAULT_MADS_PORT;
	config->logSink = LOG_SINK_FILE;
	config->logDebugAlso = LOG_LEVEL_WARN;
	config->logBufferKb = LOG_DEFAULT_BUFFER_KB;
	config->logFlushMs = LOG_DEFAULT_FLUSH_MS;
	Text_Append(launcher->logDir, 0, sizeof(launcher->logDir), DEFAULT_LOG_DIR);
	launcher->logKeep = DEFAULT_LOG_KEEP;

	Ini_Open(&reader, text, len);
	while (Ini_Next(&reader, &line)) {
		BOOL ok;
		if (Text_EqualsNoCase(line.section, line.sectionLen, "backend"))
			ok = SetBackendKey(line, launcher, &hostSet, warn);
		else if (Text_EqualsNoCase(line.section, line.sectionLen, "log"))
			ok = SetLogKey(line, launcher, warn);
		else
			ok = Unknown(line, warn);
		if (!ok)
			valid = FALSE;
	}
	if (!hostSet) {
		warn("Launcher: config: no valid [backend] host");
		valid = FALSE;
	}
	return valid;
}
