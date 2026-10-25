#include "LogConfig.h"
#include "Format.h"
#include "Text.h"

static const CHAR *const kSinkNames[] = {"off", "debug", "file", "both"}; /* by LOG_SINK_* bits */
static const CHAR *const kLevelNames[] = {"error", "warn", "info", "debug"};

BOOL LogConfig_ParseSink(const CHAR *v, DWORD n, DWORD *out)
{
	DWORD i = 0;
	while (i < 4) {
		if (Text_EqualsNoCase(v, n, kSinkNames[i])) {
			*out = i;
			return TRUE;
		}
		i++;
	}
	return FALSE;
}

const CHAR *LogConfig_SinkName(DWORD sink)
{
	return kSinkNames[sink & (LOG_SINK_DEBUG | LOG_SINK_FILE)];
}

const CHAR *LogConfig_LevelName(DWORD level)
{
	return level <= LOG_LEVEL_DEBUG ? kLevelNames[level] : "none";
}

BOOL LogConfig_ParseLevel(const CHAR *v, DWORD n, BOOL allowNone, DWORD *out)
{
	DWORD i = 0;
	while (i < 4) {
		if (Text_EqualsNoCase(v, n, kLevelNames[i])) {
			*out = i;
			return TRUE;
		}
		i++;
	}
	if (allowNone && Text_EqualsNoCase(v, n, "none")) {
		*out = LOG_LEVEL_NONE;
		return TRUE;
	}
	return FALSE;
}

BOOL LogConfig_BufferKbValid(DWORD kb)
{
	return kb >= LOG_BUFFER_MIN_KB && kb <= LOG_BUFFER_MAX_KB && (kb & (kb - 1)) == 0;
}

static BOOL IsTagSeparator(CHAR c)
{
	return c == ',' || c == ' ' || c == '\t';
}

BOOL LogConfig_TagAllowed(const CHAR *filter, const CHAR *tag)
{
	DWORD pos = 0;
	BOOL anyKeep = FALSE, kept = FALSE;
	if (filter == NULL || tag == NULL)
		return TRUE;
	while (filter[pos] != 0) {
		DWORD start, len;
		BOOL drop = FALSE;
		if (IsTagSeparator(filter[pos])) {
			pos++;
			continue;
		}
		if (filter[pos] == '-' || filter[pos] == '+') {
			drop = filter[pos] == '-';
			pos++;
		}
		start = pos;
		while (filter[pos] != 0) {
			if (IsTagSeparator(filter[pos]))
				break;
			pos++;
		}
		len = pos - start;
		if (len == 0)
			continue;
		if (!drop)
			anyKeep = TRUE;
		if (Text_EqualsNoCase(filter + start, len, tag)) {
			if (drop)
				return FALSE;
			kept = TRUE;
		}
	}
	return !anyKeep || kept;
}

VOID LogConfig_FormatCaptureLine(
	CHAR *buf, DWORD *pos, DWORD cap, DWORD tickMs, DWORD threadId, const CHAR *text, DWORD len)
{
	BOOL lf = len > 0 && text[len - 1] == '\n';
	DWORD p = *pos, i = 0;
	if (cap == 0 || p + 1 >= cap)
		return;
	if (lf)
		len--;
	QuizFmtUint(buf, &p, cap - 1, tickMs, 10, 0, 6, ' ');
	p = Text_Append(buf, p, cap - 1, "ms [line] debugstr thread=0x");
	QuizFmtUint(buf, &p, cap - 1, threadId, 16, 0, 8, '0');
	p = Text_Append(buf, p, cap - 1, lf ? " lf string=" : " string=");
	while (i < len && p + 1 < cap) {
		buf[p] = text[i];
		p++;
		i++;
	}
	buf[p++] = '\n';
	*pos = p;
}
