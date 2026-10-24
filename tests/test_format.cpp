#include "check.h"
#include <string.h>
#include "../sysdll/platform/Format.h"

static char g_Out[64];

static const char *Fmt(DWORD cap, const char *fmt, ...)
{
	DWORD pos = 0;
	va_list args;
	va_start(args, fmt);
	QuizFormatInto(g_Out, &pos, cap, fmt, args);
	va_end(args);
	g_Out[pos] = 0;
	return g_Out;
}

#define CHECK_FMT(expected, ...)                                                                \
	do {                                                                                        \
		const char *got_ = Fmt(sizeof(g_Out), __VA_ARGS__);                                     \
		if (strcmp(got_, expected) != 0) {                                                      \
			printf("%s:%d: got \"%s\", expected \"%s\"\n", __FILE__, __LINE__, got_, expected); \
			g_Failures++;                                                                       \
		}                                                                                       \
	} while (0)

void TestFormat()
{
	CHECK_FMT("plain", "plain");
	CHECK_FMT("-42 42", "%d %i", -42, 42);
	CHECK_FMT("4294967295", "%u", 0xFFFFFFFFu);
	CHECK_FMT("ff FF", "%x %X", 0xFFu, 0xFFu);
	CHECK_FMT("0000ABCD", "%08X", 0xABCDu);
	CHECK_FMT("-007", "%04d", -7);
	CHECK_FMT("   7|7   |", "%4u|%-4u|", 7u, 7u);
	CHECK_FMT("[  ab][ab  ]", "[%4s][%-4s]", "ab", "ab");
	CHECK_FMT("[-5  ][7F  ]", "[%-4d][%-4X]", -5, 0x7Fu);
	CHECK_FMT("(null)", "%s", (const char *)0);
	CHECK_FMT("x=A", "x=%c", 'A');
	CHECK_FMT("100%", "100%%");
	CHECK_FMT("%q", "%q");
	/* 'l' / 'll' read one 32-bit slot, like the console */
	CHECK_FMT("5 6", "%lu %llx", 5u, 6u);
	/* the manifest line shape */
	CHECK_FMT("[patch] id=\"QNet\" result=installed site=8223AC48 why=-",
		"[patch] id=\"%s\" result=%s site=%08X why=%s", "QNet", "installed", 0x8223AC48u, "-");

	/* exhaustion: never past cap - 1 */
	{
		const char *got = Fmt(6, "%s", "abcdefgh");
		CHECK(strcmp(got, "abcde") == 0);
		got = Fmt(6, "%08X", 0x12345678u);
		CHECK(strcmp(got, "12345") == 0);
		got = Fmt(1, "abc");
		CHECK(got[0] == 0);
	}
	{
		CHAR buf[16];
		DWORD pos = 0;
		QuizFmtUint(buf, &pos, sizeof(buf), 255, 10, 0, 0, ' ');
		buf[pos] = 0;
		CHECK(strcmp(buf, "255") == 0);
	}
}
