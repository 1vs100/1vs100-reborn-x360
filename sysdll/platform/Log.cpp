#include "Log.h"

BOOL Log_Every(volatile LONG *counter, LONG n)
{
	return (InterlockedIncrement(counter) - 1) % n == 0;
}

BOOL Log_First(volatile LONG *counter, LONG n)
{
	return InterlockedIncrement(counter) <= n;
}

BOOL Log_FirstSeen(LogSeenSet *set, DWORD key)
{
	DWORD i = 0, n = set->count;
	while (i < n && i < LOG_SEEN_SLOTS) {
		if (set->keys[i] == key)
			return FALSE;
		i++;
	}
	if (n < LOG_SEEN_SLOTS) {
		set->keys[n] = key;
		set->count = n + 1;
	}
	return TRUE;
}

static const CHAR kLevelLetter[] = {'E', 'W', 'I', 'D'};

BOOL Log_Wanted(LogLevel level, const CHAR *tag);
VOID Log_RouteBuilt(LogLevel level, const CHAR *text);

VOID Log_Write(LogLevel level, const CHAR *tag, const CHAR *fmt, ...)
{
	CHAR buf[512];
	DWORD pos = 0;
	va_list args;

	/* Filter first: a dropped line costs no formatting. */
	if (!Log_Wanted(level, tag))
		return;
	/* Built by hand: no CRT. */
	while (pos < 4) {
		buf[pos] = "[QS]"[pos];
		pos++;
	}
	buf[pos++] = '[';
	buf[pos++] = kLevelLetter[level <= LOG_LEVEL_DEBUG ? level : LOG_LEVEL_DEBUG];
	buf[pos++] = ']';
	buf[pos++] = '[';
	while (*tag != 0 && pos < 24) {
		buf[pos] = *tag;
		pos++;
		tag++;
	}
	buf[pos++] = ']';
	buf[pos++] = ' ';

	va_start(args, fmt);
	QuizFormatInto(buf, &pos, sizeof(buf) - 1, fmt, args);
	va_end(args);
	buf[pos++] = '\n';
	buf[pos] = 0;
	Log_RouteBuilt(level, buf);
}
