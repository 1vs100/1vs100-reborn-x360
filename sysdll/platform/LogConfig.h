#pragma once
#include "Types.h"

/* Shared log settings: parsed by the launcher, used by the sysdll. */

enum LogLevel { LOG_LEVEL_ERROR, LOG_LEVEL_WARN, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG };

/* Bits; 0 is off. */
#define LOG_SINK_DEBUG 1u
#define LOG_SINK_FILE 2u

#define LOG_LINE_BYTES 512 /* one line with its newline and NUL */
#define LOG_PATH_BYTES 160 /* device path */
#define LOG_TAGS_BYTES 128 /* as written */
#define LOG_DEFAULT_FLUSH_MS 100
#define LOG_BUFFER_MIN_KB 64
#define LOG_BUFFER_MAX_KB 16384
#define LOG_DEFAULT_BUFFER_KB 2048

/* off, debug, file, both. */
BOOL LogConfig_ParseSink(const CHAR *v, DWORD n, DWORD *out);
const CHAR *LogConfig_SinkName(DWORD sink);
/* error, warn, info, debug; with allowNone, also none. */
#define LOG_LEVEL_NONE 0xFFFFFFFFu
BOOL LogConfig_ParseLevel(const CHAR *v, DWORD n, BOOL allowNone, DWORD *out);
/* "none" past debug. */
const CHAR *LogConfig_LevelName(DWORD level);

/* Power of two, LOG_BUFFER_MIN_KB..LOG_BUFFER_MAX_KB: the ring indexes with a mask. */
BOOL LogConfig_BufferKbValid(DWORD kb);

/* "-tag" drops, "tag"/"+tag" keeps; empty keeps all. Case-insensitive. */
BOOL LogConfig_TagAllowed(const CHAR *filter, const CHAR *tag);

/* XBDM capture shape, so log files and captures share tools. Overlong text is cut, the line still ends with newline. */
VOID LogConfig_FormatCaptureLine(
	CHAR *buf, DWORD *pos, DWORD cap, DWORD tickMs, DWORD threadId, const CHAR *text, DWORD len);
