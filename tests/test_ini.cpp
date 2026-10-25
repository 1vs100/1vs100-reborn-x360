#include "check.h"
#include <string.h>
#include "../sysdll/platform/Ini.h"

static BOOL Is(const CHAR *p, DWORD n, const char *expected)
{
	return n == strlen(expected) && memcmp(p, expected, n) == 0;
}

void TestIni()
{
	/* CRLF, comments, blanks, odd spacing, sections */
	const char text[] = "; comment\r\n"
						"top=1\r\n"
						"[ backend ]\r\n"
						"\r\n"
						"  host = 192.168.1.10 \r\n"
						"# another comment = with equals\n"
						"no equals here\n"
						"[log\n"
						"sink=\tfile";
	IniReader reader;
	IniLine line;

	Ini_Open(&reader, text, sizeof(text) - 1);
	CHECK(Ini_Next(&reader, &line) && line.sectionLen == 0 && Is(line.key, line.keyLen, "top"));
	CHECK(Ini_Next(&reader, &line) && Is(line.section, line.sectionLen, "backend"));
	CHECK(Is(line.key, line.keyLen, "host") && Is(line.value, line.valueLen, "192.168.1.10"));
	CHECK(Ini_Next(&reader, &line) && Is(line.section, line.sectionLen, "log"));
	CHECK(Is(line.key, line.keyLen, "sink") && Is(line.value, line.valueLen, "file"));
	CHECK(!Ini_Next(&reader, &line));
	CHECK(!Ini_Next(&reader, &line));

	/* empty value, empty input */
	Ini_Open(&reader, "key=\n", 5);
	CHECK(Ini_Next(&reader, &line) && Is(line.key, line.keyLen, "key") && line.valueLen == 0);
	Ini_Open(&reader, "", 0);
	CHECK(!Ini_Next(&reader, &line));

	{
		DWORD a = 7;
		CHECK(Ini_ParseIPv4("192.168.42.1", 12, &a) && a == 0xC0A82A01u);
		CHECK(Ini_ParseIPv4("0.0.0.0", 7, &a) && a == 0);
		a = 7;
		CHECK(!Ini_ParseIPv4("256.1.1.1", 9, &a) && a == 7);
		CHECK(!Ini_ParseIPv4("1.2.3", 5, &a));
		CHECK(!Ini_ParseIPv4("1.2.3.4.5", 9, &a));
		CHECK(!Ini_ParseIPv4("1..3.4", 6, &a));
		CHECK(!Ini_ParseIPv4("1.2.3.4 ", 8, &a));
		CHECK(!Ini_ParseIPv4("", 0, &a) && a == 7);
		CHECK(Ini_ParseIPv4("001.002.003.004", 15, &a) && a == 0x01020304u);
		CHECK(!Ini_ParseIPv4("0001.2.3.4", 10, &a));
	}
	CHECK(Ini_IsHostname("backend", 7));
	CHECK(Ini_IsHostname("1vs100.lan", 10));
	CHECK(Ini_IsHostname("a-b.example.org", 15));
	CHECK(!Ini_IsHostname("1.2.3", 5));
	CHECK(!Ini_IsHostname("192.168.42.1", 12));
	CHECK(!Ini_IsHostname("", 0));
	CHECK(!Ini_IsHostname("host.", 5));
	CHECK(!Ini_IsHostname(".host", 5));
	CHECK(!Ini_IsHostname("a..b", 4));
	CHECK(!Ini_IsHostname("-host", 5));
	CHECK(!Ini_IsHostname("host-", 5));
	CHECK(!Ini_IsHostname("a-.b", 4));
	CHECK(!Ini_IsHostname("a.-b", 4));
	CHECK(!Ini_IsHostname("ho st", 5));
	CHECK(!Ini_IsHostname("host:80", 7));
}
