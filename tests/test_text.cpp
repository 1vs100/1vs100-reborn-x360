#include "check.h"
#include <string.h>
#include "../sysdll/platform/Text.h"

void TestText()
{
	CHAR buf[8];
	DWORD pos;

	CHECK(Text_Length("abc", 10) == 3);
	CHECK(Text_Length("abc", 2) == 2);
	CHECK(Text_Length("", 10) == 0);

	/* Append: truncates at cap - 1, always terminated */
	pos = Text_Append(buf, 0, sizeof(buf), "abc");
	CHECK(pos == 3 && strcmp(buf, "abc") == 0);
	pos = Text_Append(buf, pos, sizeof(buf), "defghij");
	CHECK(pos == 7 && strcmp(buf, "abcdefg") == 0);
	pos = Text_Append(buf, pos, sizeof(buf), "x");
	CHECK(pos == 7 && strcmp(buf, "abcdefg") == 0);
	pos = Text_Append(buf, 0, sizeof(buf), "");
	CHECK(pos == 0 && buf[0] == 0);
	CHECK(Text_Append(buf, 0, 0, "abc") == 0);
	pos = Text_Append(buf, 0, 1, "abc");
	CHECK(pos == 0 && buf[0] == 0);

	/* AppendWide: non-ASCII -> '?', maxChars, capacity */
	{
		const WCHAR w[] = {'G', 'a', 0x00E9, 'm', 0x4E2D, 0};
		pos = Text_AppendWide(buf, 0, sizeof(buf), w, 100);
		CHECK(pos == 5 && strcmp(buf, "Ga?m?") == 0);
		pos = Text_AppendWide(buf, 0, sizeof(buf), w, 2);
		CHECK(pos == 2 && strcmp(buf, "Ga") == 0);
		pos = Text_AppendWide(buf, 0, 4, w, 100);
		CHECK(pos == 3 && strcmp(buf, "Ga?") == 0);
	}

	/* AppendHex: the content directory's title id component */
	{
		CHAR path[48];
		pos = Text_Append(path, 0, sizeof(path), "\\Content\\0000000000000000\\");
		pos = Text_AppendHex(path, pos, sizeof(path), 0x584A07E4u, 8);
		pos = Text_Append(path, pos, sizeof(path), "\\00000002");
		CHECK(strcmp(path, "\\Content\\0000000000000000\\584A07E4\\00000002") == 0);
		CHECK(pos == 43);
		pos = Text_AppendHex(buf, 0, sizeof(buf), 0xABCDu, 4);
		CHECK(strcmp(buf, "ABCD") == 0);
		pos = Text_AppendHex(buf, 0, 4, 0x12345678u, 8);
		CHECK(pos == 3 && strcmp(buf, "123") == 0);
	}

	{
		CHAR dec[24];
		CHECK(Text_FormatDecimalU64(0, dec) == 1 && strcmp(dec, "0") == 0);
		CHECK(Text_FormatDecimalU64(18446744073709551615ULL, dec) == 20 &&
			strcmp(dec, "18446744073709551615") == 0);
		CHECK(Text_FormatDecimalU64(0x0009000006F1A2B3ULL, dec) > 0 &&
			strcmp(dec, "2533274906895027") == 0);
	}

	CHECK(
		Text_LowerAscii('A') == 'a' && Text_LowerAscii('z') == 'z' && Text_LowerAscii('@') == '@');

	CHECK(Text_Contains("game:\\1vs100-fre.xex", "1vs100"));
	CHECK(Text_Contains("abc", "c"));
	CHECK(!Text_Contains("abc", "abcd"));
	CHECK(!Text_Contains("abc", ""));
	CHECK(!Text_Contains(0, "a"));
	CHECK(Text_StartsWith("[%x]:--Block[%i]", "[%x]:--Block"));
	CHECK(!Text_StartsWith("[%x]:Blo", "[%x]:Block++"));

	CHECK(Text_EqualsNoCase("Sbg_Port", 8, "sbg_port"));
	CHECK(!Text_EqualsNoCase("sbg_por", 7, "sbg_port"));
	CHECK(!Text_EqualsNoCase("sbg_portx", 9, "sbg_port"));
	CHECK(Text_EqualsNoCase("", 0, ""));

	{
		DWORD v = 7;
		CHECK(Text_ParseUint("1", 1, 1, 65535, &v) && v == 1);
		CHECK(Text_ParseUint("65535", 5, 1, 65535, &v) && v == 65535);
		v = 7;
		CHECK(!Text_ParseUint("0", 1, 1, 65535, &v) && v == 7);
		CHECK(!Text_ParseUint("65536", 5, 1, 65535, &v) && v == 7);
		CHECK(!Text_ParseUint("99999999999", 11, 1, 65535, &v));
		CHECK(!Text_ParseUint("", 0, 0, 10, &v));
		CHECK(!Text_ParseUint("10a", 3, 0, 100, &v));
		CHECK(!Text_ParseUint("-1", 2, 0, 100, &v) && v == 7);
		CHECK(!Text_ParseUint("5", 1, 0, 3, &v));
		CHECK(Text_ParseUint("0004294967295", 13, 0, 0xFFFFFFFFu, &v) && v == 0xFFFFFFFFu);
		CHECK(!Text_ParseUint("4294967296", 10, 0, 0xFFFFFFFFu, &v));
	}
}
