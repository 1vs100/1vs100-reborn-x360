#pragma once
#include "Format.h"
#include "LogConfig.h"

/* One line per call, routed by the [log] settings to the debug channel, the log file, or both. */
/* Format strings have no trailing '\n'; lines longer than LOG_LINE_BYTES - 2 are cut. */

VOID Log_Write(LogLevel level, const CHAR *tag, const CHAR *fmt, ...);
/* NUL-terminated line built elsewhere, routed the same way. Only this and Log_Write call OutputDebugStringA. */
VOID Log_EmitText(LogLevel level, const CHAR *tag, const CHAR *text);

/* Until settings arrive, lines go to the debug channel and are kept for the file. */
struct LogSettings {
	DWORD sink;        /* LOG_SINK_* bits */
	BOOL levelSet;     /* FALSE: the build's default */
	DWORD level;       /* most verbose level kept */
	DWORD debugAlso;   /* file-only sink: this level and above also to the debug channel */
	DWORD bufferKb;    /* LogConfig_BufferKbValid */
	DWORD flushMs;
	const CHAR *file; /* NULL or empty: no file */
	DWORD fileBytes;  /* already written by the launcher */
	const CHAR *tags; /* NULL or empty: every tag */
};
VOID Log_Configure(const LogSettings *settings);

/* Writer thread body, flush request, shutdown. */
DWORD Log_WriterMain(PVOID unused);
VOID Log_RequestFlush();
VOID Log_Shutdown(DWORD waitMs);

#define LOG_AT(level, tag, fmt, ...) Log_Write((level), (tag), fmt, __VA_ARGS__)

#define LOG_ERROR(tag, fmt, ...) LOG_AT(LOG_LEVEL_ERROR, tag, fmt, __VA_ARGS__)
#define LOG_WARN(tag, fmt, ...) LOG_AT(LOG_LEVEL_WARN, tag, fmt, __VA_ARGS__)
#define LOG_INFO(tag, fmt, ...) LOG_AT(LOG_LEVEL_INFO, tag, fmt, __VA_ARGS__)
#define LOG_DEBUG(tag, fmt, ...) LOG_AT(LOG_LEVEL_DEBUG, tag, fmt, __VA_ARGS__)

/* TRUE on call 1, n + 1, 2n + 1... Log_First: TRUE on calls 1..n only. Counts in the caller counter. */
BOOL Log_Every(volatile LONG *counter, LONG n);
BOOL Log_First(volatile LONG *counter, LONG n);

/* Best effort, no lock: a race costs a duplicate line. Once full, every further key reads as new. */
#define LOG_SEEN_SLOTS 96
struct LogSeenSet {
	DWORD keys[LOG_SEEN_SLOTS];
	DWORD count;
};
BOOL Log_FirstSeen(LogSeenSet *set, DWORD key);
