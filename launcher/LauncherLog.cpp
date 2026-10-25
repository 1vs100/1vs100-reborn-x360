#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "LauncherLog.h"
#include "DeviceFile.h"
#include "../sysdll/platform/Text.h"

// The CRT guard covers sysdll title threads; the launcher may use the CRT.
#pragma warning(disable : 4995)

#define LOG_SEQUENCE_FILE "next.txt"
#define LOG_LATEST_FILE "latest.txt"

/* Before config, launcher routing, then the sysdll. */
enum LogPhase { LOG_BEFORE_CONFIG, LOG_LAUNCHER, LOG_THROUGH_SYSDLL };
static LogPhase g_LogPhase = LOG_BEFORE_CONFIG;
static CHAR g_LogPending[32 * 1024]; /* lines until the file exists */
static DWORD g_LogPendingLen = 0;
static DWORD g_LogPendingDropped = 0;
static DWORD g_LogSink = LOG_SINK_DEBUG;
static BOOL g_LogWanted = TRUE;       /* level/tag filter for launcher lines */
static BOOL g_LogDebugAlso = FALSE;   /* file-only, info also to debug */
static CHAR g_LogFile[LOG_PATH_BYTES]; /* empty is no file */
static DWORD g_LogFileBytes = 0;
static RevivalLogLine_t g_SysdllLog = NULL;

void Log(const CHAR *fmt, ...)
{
	CHAR buf[LOG_LINE_BYTES];
	CHAR line[LOG_LINE_BYTES + 64];
	DWORD pos = 0;
	va_list args;
	va_start(args, fmt);
	_vsnprintf(buf, sizeof(buf) - 2, fmt, args);
	va_end(args);
	buf[sizeof(buf) - 2] = 0;
	{
		size_t n = strlen(buf);
		buf[n] = '\n';
		buf[n + 1] = 0;
	}
	if (g_LogPhase == LOG_THROUGH_SYSDLL) {
		g_SysdllLog(buf);
		return;
	}
	if (!g_LogWanted)
		return;
	if (g_LogPhase == LOG_BEFORE_CONFIG || (g_LogSink & LOG_SINK_DEBUG) || g_LogDebugAlso)
		OutputDebugStringA(buf);
	if (g_LogPhase == LOG_LAUNCHER && !g_LogFile[0])
		return;
	LogConfig_FormatCaptureLine(line, &pos, sizeof(line), GetTickCount(), GetCurrentThreadId(),
		buf, (DWORD)strlen(buf));
	if (g_LogPhase == LOG_BEFORE_CONFIG) {
		if (g_LogPendingLen + pos <= sizeof(g_LogPending)) {
			memcpy(g_LogPending + g_LogPendingLen, line, pos);
			g_LogPendingLen += pos;
		} else {
			g_LogPendingDropped++;
		}
		return;
	}
	if (WriteDeviceFile(g_LogFile, line, pos, g_LogFileBytes, FALSE) >= 0)
		g_LogFileBytes += pos;
}

DWORD LauncherLog_FileBytes()
{
	return g_LogFileBytes;
}

VOID LauncherLog_ThroughSysdll(RevivalLogLine_t logLine)
{
	g_SysdllLog = logLine;
	g_LogPhase = LOG_THROUGH_SYSDLL;
}

VOID LauncherLog_Open(RevivalLaunchConfig *config, const CHAR *logDir, DWORD logKeep)
{
	CHAR dir[MAX_PATH], path[MAX_PATH], text[96];
	NTSTATUS st = 0;
	DWORD runNumber = 1; /* next.txt missing or invalid */
	g_LogSink = config->logSink;
	g_LogWanted = config->logSink != 0 &&
		(!config->logLevelSet || config->logLevel >= LOG_LEVEL_INFO) &&
		LogConfig_TagAllowed(config->logTags, "launcher");
	g_LogDebugAlso = !(config->logSink & LOG_SINK_DEBUG) &&
		config->logDebugAlso != LOG_LEVEL_NONE && config->logDebugAlso >= LOG_LEVEL_INFO;
	g_LogPhase = LOG_LAUNCHER;
	if (!(config->logSink & LOG_SINK_FILE))
		return;

	/* Each level from the HDD root down. */
	_snprintf(dir, sizeof(dir) - 1, "%s", HDD_ROOT);
	dir[sizeof(dir) - 1] = 0;
	for (const CHAR *part = logDir; *part && st >= 0;) {
		const CHAR *end = strchr(part, '\\');
		size_t n = end ? (size_t)(end - part) : strlen(part);
		size_t at = strlen(dir);
		if (n > 0 && at + 1 + n < sizeof(dir)) {
			dir[at] = '\\';
			memcpy(dir + at + 1, part, n);
			dir[at + 1 + n] = 0;
			st = CreateDeviceDirectory(dir);
		}
		part = end ? end + 1 : part + n;
	}
	if (st < 0) {
		Log("Launcher: log: cannot create %s (%08X), logging to the debug channel", dir, st);
		config->logSink = g_LogSink = LOG_SINK_DEBUG;
		return;
	}

	_snprintf(path, sizeof(path) - 1, "%s\\%s", dir, LOG_SEQUENCE_FILE);
	path[sizeof(path) - 1] = 0;
	LONG got = ReadDeviceFile(path, text, sizeof(text), &st);
	if (got > 0) {
		text[got] = 0;
		DWORD n = 0;
		while (n < (DWORD)got && text[n] >= '0' && text[n] <= '9')
			n++;
		Text_ParseUint(text, n, 1, 0xFFFFFFFEu, &runNumber);
	}
	_snprintf(text, sizeof(text) - 1, "%u\r\n", runNumber + 1);
	text[sizeof(text) - 1] = 0;
	WriteDeviceFile(path, text, (DWORD)strlen(text), 0, TRUE);

	/* Oldest past keep; stops at a gap. */
	for (DWORD old = runNumber > logKeep ? runNumber - logKeep : 0; old > 0; --old) {
		_snprintf(path, sizeof(path) - 1, "%s\\run-%06u.log", dir, old);
		path[sizeof(path) - 1] = 0;
		if (DeleteDeviceFile(path) < 0)
			break;
	}

	_snprintf(path, sizeof(path) - 1, "%s\\run-%06u.log", dir, runNumber);
	path[sizeof(path) - 1] = 0;
	if (strlen(path) >= sizeof(g_LogFile)) {
		Log("Launcher: log: path %s too long, logging to the debug channel", path);
		config->logSink = g_LogSink = LOG_SINK_DEBUG;
		return;
	}
	st = WriteDeviceFile(path, g_LogPending, g_LogPendingLen, 0, TRUE);
	if (st < 0) {
		Log("Launcher: log: cannot write %s (%08X), logging to the debug channel", path, st);
		config->logSink = g_LogSink = LOG_SINK_DEBUG;
		return;
	}
	strcpy(g_LogFile, path);
	g_LogFileBytes = g_LogPendingLen;
	Text_Append(config->logFile, 0, sizeof(config->logFile), path);

	_snprintf(text, sizeof(text) - 1, "Hdd:\\%s\\run-%06u.log\r\n", logDir, runNumber);
	text[sizeof(text) - 1] = 0;
	_snprintf(path, sizeof(path) - 1, "%s\\%s", dir, LOG_LATEST_FILE);
	path[sizeof(path) - 1] = 0;
	WriteDeviceFile(path, text, (DWORD)strlen(text), 0, TRUE);

	if (g_LogPendingDropped)
		Log("Launcher: log: %u early line(s) did not fit the start-up buffer", g_LogPendingDropped);
	Log("Launcher: log file %s (keeping %u)", g_LogFile, logKeep);
}
