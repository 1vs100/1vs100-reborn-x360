#pragma once
#include "Types.h"

/* Shared log settings: parsed by the launcher, used by the sysdll. */

/* Bits; 0 is off. */
#define LOG_SINK_DEBUG 1u
#define LOG_SINK_FILE 2u

#define LOG_PATH_BYTES 160 /* device path */
#define LOG_TAGS_BYTES 128 /* as written */

/* debug, file, both, off. */
BOOL LogConfig_ParseSink(const CHAR *v, DWORD n, DWORD *out);
/* error, warn, info, debug; with allowNone, also none. */
#define LOG_LEVEL_NONE 0xFFFFFFFFu
BOOL LogConfig_ParseLevel(const CHAR *v, DWORD n, BOOL allowNone, DWORD *out);
/* Digits only, at most 9. */
BOOL LogConfig_ParseNumber(const CHAR *v, DWORD n, DWORD *out);

/* "-tag" drops, "tag"/"+tag" keeps; empty keeps all. Case-insensitive. */
BOOL LogConfig_TagAllowed(const CHAR *filter, const CHAR *tag);

/* XBDM capture shape, so log files and captures share tools. Overlong text is cut, the line still ends with newline. */
VOID LogConfig_FormatCaptureLine(
	CHAR *buf, DWORD *pos, DWORD cap, DWORD tickMs, DWORD threadId, const CHAR *text, DWORD len);
