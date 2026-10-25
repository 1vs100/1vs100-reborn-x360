#include "LauncherConfig.h"
#include "../sysdll/platform/Ini.h"
#include "../sysdll/platform/Text.h"

#define DEFAULT_BACKEND "192.168.42.1"
#define DEFAULT_BACKEND_IN_ADDR 0xC0A82A01u
#define DEFAULT_SBG_PORT 1000
#define DEFAULT_MATCHMAKING_PORT 1001
#define DEFAULT_LOG_DIR "1vs100Reborn\\logs"
#define DEFAULT_LOG_KEEP 10

static VOID SetUint(const IniLine &line, DWORD min, DWORD max, DWORD *out, ConfigWarn_t warn)
{
	if (!Text_ParseUint(line.value, line.valueLen, min, max, out))
		warn("Launcher: config: %.*s is not %u-%u, keeping %u", (int)line.keyLen, line.key, min, max,
			*out);
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

/* One [log] key; FALSE when not one. A bad value keeps the default. */
static BOOL ReadLogKey(const IniLine &line, LauncherConfig *launcher, ConfigWarn_t warn)
{
	RevivalLaunchConfig *config = &launcher->handover;
	const CHAR *v = line.value;
	DWORD n = line.valueLen;
	if (Text_EqualsNoCase(line.key, line.keyLen, "log_sink")) {
		if (!LogConfig_ParseSink(v, n, &config->logSink))
			warn("Launcher: config: log_sink is not debug, file, both or off, keeping the default");
	} else if (Text_EqualsNoCase(line.key, line.keyLen, "log_level")) {
		if (LogConfig_ParseLevel(v, n, FALSE, &config->logLevel))
			config->logLevelSet = TRUE;
		else
			warn("Launcher: config: log_level is not error, warn, info or debug, keeping the "
				"build's default");
	} else if (Text_EqualsNoCase(line.key, line.keyLen, "log_debug_also")) {
		if (!LogConfig_ParseLevel(v, n, TRUE, &config->logDebugAlso))
			warn("Launcher: config: log_debug_also is not error, warn, info, debug or none, "
				"keeping warn");
	} else if (Text_EqualsNoCase(line.key, line.keyLen, "log_buffer_kb")) {
		DWORD value;
		if (Text_ParseUint(v, n, 0, LOG_BUFFER_MAX_KB, &value) && LogConfig_BufferKbValid(value))
			config->logBufferKb = value;
		else
			warn("Launcher: config: log_buffer_kb is not a power of two in %u-%u, keeping %u",
				LOG_BUFFER_MIN_KB, LOG_BUFFER_MAX_KB, config->logBufferKb);
	} else if (Text_EqualsNoCase(line.key, line.keyLen, "log_flush_ms")) {
		SetUint(line, 100, 60000, &config->logFlushMs, warn);
	} else if (Text_EqualsNoCase(line.key, line.keyLen, "log_tags")) {
		if (n < sizeof(config->logTags))
			Text_Append(config->logTags, 0, n + 1, v);
		else
			warn("Launcher: config: log_tags longer than %u characters, ignored",
				(DWORD)sizeof(config->logTags) - 1);
	} else if (Text_EqualsNoCase(line.key, line.keyLen, "log_dir")) {
		while (n > 0 && v[n - 1] == '\\')
			n--;
		if (IsLogDir(v, n))
			Text_Append(launcher->logDir, 0, n + 1, v);
		else
			warn("Launcher: config: log_dir is not a plain path below the HDD root, keeping %s",
				launcher->logDir);
	} else if (Text_EqualsNoCase(line.key, line.keyLen, "log_keep")) {
		SetUint(line, 1, 1000, &launcher->logKeep, warn);
	} else {
		return FALSE;
	}
	return TRUE;
}

VOID LauncherConfig_Parse(const CHAR *text, DWORD len, LauncherConfig *launcher, ConfigWarn_t warn)
{
	RevivalLaunchConfig *config = &launcher->handover;
	IniLine line;
	DWORD pos = 0;

	*launcher = LauncherConfig();
	config->size = sizeof(*config);
	config->backendInAddr = DEFAULT_BACKEND_IN_ADDR;
	config->sbgPort = DEFAULT_SBG_PORT;
	config->matchmakingPort = DEFAULT_MATCHMAKING_PORT;
	Text_Append(config->backendHost, 0, sizeof(config->backendHost), DEFAULT_BACKEND);
	config->logSink = LOG_SINK_FILE;
	config->logDebugAlso = LOG_LEVEL_WARN;
	config->logBufferKb = LOG_DEFAULT_BUFFER_KB;
	config->logFlushMs = LOG_DEFAULT_FLUSH_MS;
	Text_Append(launcher->logDir, 0, sizeof(launcher->logDir), DEFAULT_LOG_DIR);
	launcher->logKeep = DEFAULT_LOG_KEEP;

	while (Ini_Next(text, len, &pos, &line)) {
		if (Text_EqualsNoCase(line.key, line.keyLen, "address")) {
			DWORD addr;
			if (line.valueLen > REVIVAL_GRID_HOST_MAX) {
				warn("Launcher: config: address longer than %u characters (Grid URL), keeping %s",
					(unsigned)REVIVAL_GRID_HOST_MAX, config->backendHost);
				continue;
			}
			if (Ini_ParseIPv4(line.value, line.valueLen, &addr)) {
				config->backendInAddr = addr;
				launcher->hostIsName = FALSE;
			} else if (Ini_IsHostname(line.value, line.valueLen)) {
				launcher->hostIsName = TRUE;
			} else {
				warn("Launcher: config: address is neither an IPv4 address nor a host name, "
					 "keeping %s",
					config->backendHost);
				continue;
			}
			Text_Append(config->backendHost, 0, line.valueLen + 1, line.value);
		} else if (Text_EqualsNoCase(line.key, line.keyLen, "sbg_port")) {
			SetUint(line, 1, 65535, &config->sbgPort, warn);
		} else if (Text_EqualsNoCase(line.key, line.keyLen, "matchmaking_port")) {
			SetUint(line, 1, 65535, &config->matchmakingPort, warn);
		} else if (!ReadLogKey(line, launcher, warn)) {
			warn("Launcher: config: unknown key '%.*s' ignored", (int)line.keyLen, line.key);
		}
	}
}
