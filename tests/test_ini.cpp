#include "check.h"
#include <string.h>
#include "../sysdll/platform/Ini.h"

static BOOL Is(const CHAR *p, DWORD n, const char *expected)
{
	return n == strlen(expected) && memcmp(p, expected, n) == 0;
}

void TestIni()
{
	/* the shipped example, with CRLF, a comment, blanks and odd spacing */
	const char text[] = "; Copy to the HDD root\r\n"
						"[revival]\r\n"
						"\r\n"
						"  address = 192.168.1.10 \r\n"
						"sbg_port=1000\r\n"
						"# another comment = with equals\n"
						"no equals here\n"
						"matchmaking_port=\t1001";
	DWORD pos = 0;
	IniLine line;

	CHECK(Ini_Next(text, sizeof(text) - 1, &pos, &line));
	CHECK(Is(line.key, line.keyLen, "address") && Is(line.value, line.valueLen, "192.168.1.10"));
	CHECK(Ini_Next(text, sizeof(text) - 1, &pos, &line));
	CHECK(Is(line.key, line.keyLen, "sbg_port") && Is(line.value, line.valueLen, "1000"));
	CHECK(Ini_Next(text, sizeof(text) - 1, &pos, &line));
	CHECK(Is(line.key, line.keyLen, "matchmaking_port") && Is(line.value, line.valueLen, "1001"));
	CHECK(!Ini_Next(text, sizeof(text) - 1, &pos, &line));
	CHECK(!Ini_Next(text, sizeof(text) - 1, &pos, &line));

	/* empty value, empty input */
	pos = 0;
	CHECK(Ini_Next("key=\n", 5, &pos, &line) && Is(line.key, line.keyLen, "key") &&
		line.valueLen == 0);
	pos = 0;
	CHECK(!Ini_Next("", 0, &pos, &line));

	{
		DWORD port = 7;
		CHECK(Ini_ParsePort("1", 1, &port) && port == 1);
		CHECK(Ini_ParsePort("65535", 5, &port) && port == 65535);
		port = 7;
		CHECK(!Ini_ParsePort("0", 1, &port) && port == 7);
		CHECK(!Ini_ParsePort("65536", 5, &port) && port == 7);
		CHECK(!Ini_ParsePort("70000", 5, &port));
		CHECK(!Ini_ParsePort("99999999999", 11, &port));
		CHECK(!Ini_ParsePort("", 0, &port));
		CHECK(!Ini_ParsePort("10a", 3, &port));
		CHECK(!Ini_ParsePort("-1", 2, &port) && port == 7);
	}
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
