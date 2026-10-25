#include "check.h"
#include <string.h>
#include "../sysdll/platform/LogConfig.h"

static DWORD Sink(const char *v)
{
	DWORD out = 0xAA;
	return LogConfig_ParseSink(v, (DWORD)strlen(v), &out) ? out : 0xAA;
}

static DWORD Level(const char *v, BOOL allowNone)
{
	DWORD out = 0xAA;
	return LogConfig_ParseLevel(v, (DWORD)strlen(v), allowNone, &out) ? out : 0xAA;
}

static BOOL Line(DWORD tick, DWORD tid, const char *text, DWORD cap, const char *expected)
{
	char buf[256];
	DWORD pos = 0;
	memset(buf, 'x', sizeof(buf));
	LogConfig_FormatCaptureLine(buf, &pos, cap, tick, tid, text, (DWORD)strlen(text));
	return pos == strlen(expected) && memcmp(buf, expected, pos) == 0;
}

void TestLogConfig()
{
	CHECK(Sink("debug") == LOG_SINK_DEBUG);
	CHECK(Sink("FILE") == LOG_SINK_FILE);
	CHECK(Sink("both") == (LOG_SINK_DEBUG | LOG_SINK_FILE));
	CHECK(Sink("off") == 0);
	CHECK(Sink("hdd") == 0xAA);

	CHECK(Level("error", FALSE) == 0 && Level("Warn", FALSE) == 1);
	CHECK(Level("info", FALSE) == 2 && Level("debug", FALSE) == 3);
	CHECK(Level("none", FALSE) == 0xAA && Level("none", TRUE) == LOG_LEVEL_NONE);
	CHECK(Level("verbose", TRUE) == 0xAA);
	CHECK(strcmp(LogConfig_SinkName(LOG_SINK_DEBUG | LOG_SINK_FILE), "both") == 0);
	CHECK(strcmp(LogConfig_SinkName(0), "off") == 0);
	CHECK(strcmp(LogConfig_LevelName(LOG_LEVEL_WARN), "warn") == 0);
	CHECK(strcmp(LogConfig_LevelName(LOG_LEVEL_NONE), "none") == 0);

	CHECK(LogConfig_BufferKbValid(64) && LogConfig_BufferKbValid(2048) && LogConfig_BufferKbValid(16384));
	CHECK(!LogConfig_BufferKbValid(32) && !LogConfig_BufferKbValid(3000) && !LogConfig_BufferKbValid(32768));
	CHECK(!LogConfig_BufferKbValid(0));

	/* empty: everything; "-x": all but x; "x" / "+x": only those */
	CHECK(LogConfig_TagAllowed("", "qnet") && LogConfig_TagAllowed(NULL, "qnet"));
	CHECK(!LogConfig_TagAllowed("-netlog", "netlog") && LogConfig_TagAllowed("-netlog", "qnet"));
	CHECK(LogConfig_TagAllowed("qnet, mm", "MM") && !LogConfig_TagAllowed("qnet,mm", "lua"));
	CHECK(LogConfig_TagAllowed("+qnet -qnet", "mm") == FALSE);
	CHECK(!LogConfig_TagAllowed("+qnet,-qnet", "qnet"));
	CHECK(LogConfig_TagAllowed(" , ", "lua"));

	/* the capture shape xeedee writes, 'lf' for a text that ended with a newline */
	CHECK(Line(57481, 0xF9000004, "[QS][I][mm] joined\n", 256,
		" 57481ms [line] debugstr thread=0xf9000004 lf string=[QS][I][mm] joined\n"));
	CHECK(Line(124518, 0x10, "[LOG cat=4 lvl=3] x", 256,
		"124518ms [line] debugstr thread=0x00000010 string=[LOG cat=4 lvl=3] x\n"));
	/* cut to the buffer, still one line */
	CHECK(Line(1, 2, "abcdef", 60, "     1ms [line] debugstr thread=0x00000002 string=abcdef\n"));
	CHECK(Line(1, 2, "abcdefgh", 56, "     1ms [line] debugstr thread=0x00000002 string=abcde\n"));
}
