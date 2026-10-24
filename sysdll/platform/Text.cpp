#include "Text.h"

DWORD Text_Length(const CHAR *s, DWORD max)
{
	DWORD n = 0;
	while (n < max) {
		if (s[n] == 0)
			break;
		n++;
	}
	return n;
}

DWORD Text_Append(CHAR *dst, DWORD pos, DWORD cap, const CHAR *src)
{
	if (cap == 0)
		return pos;
	while (pos + 1 < cap) {
		if (*src == 0)
			break;
		dst[pos] = *src;
		pos++;
		src++;
	}
	if (pos < cap)
		dst[pos] = 0;
	return pos;
}

DWORD Text_AppendWide(CHAR *dst, DWORD pos, DWORD cap, const WCHAR *src, DWORD maxChars)
{
	DWORD i = 0;
	if (cap == 0)
		return pos;
	while (pos + 1 < cap && i < maxChars) {
		WCHAR ch = src[i];
		if (ch == 0)
			break;
		dst[pos] = ch <= 0x7F ? (CHAR)ch : '?';
		pos++;
		i++;
	}
	if (pos < cap)
		dst[pos] = 0;
	return pos;
}

DWORD Text_AppendHex(CHAR *dst, DWORD pos, DWORD cap, DWORD v, DWORD digits)
{
	if (cap == 0)
		return pos;
	while (digits > 0 && pos + 1 < cap) {
		DWORD nibble;
		digits--;
		nibble = (v >> (digits * 4)) & 0xF;
		dst[pos] = (CHAR)(nibble < 10 ? '0' + nibble : 'A' + (nibble - 10));
		pos++;
	}
	if (pos < cap)
		dst[pos] = 0;
	return pos;
}

DWORD Text_FormatDecimalU64(ULONGLONG v, CHAR *out)
{
	CHAR tmp[24];
	DWORD n = 0, i = 0;
	while (TRUE) {
		tmp[n] = (CHAR)('0' + (DWORD)(v % 10));
		n++;
		v /= 10;
		if (v == 0)
			break;
	}
	while (i < n) {
		out[i] = tmp[n - 1 - i];
		i++;
	}
	out[n] = 0;
	return n;
}

CHAR Text_LowerAscii(CHAR c)
{
	return (c >= 'A' && c <= 'Z') ? (CHAR)(c - 'A' + 'a') : c;
}

BOOL Text_StartsWith(const CHAR *text, const CHAR *prefix)
{
	while (*prefix != 0) {
		if (*text != *prefix)
			return FALSE;
		text++;
		prefix++;
	}
	return TRUE;
}

BOOL Text_Contains(const CHAR *haystack, const CHAR *needle)
{
	if (!haystack || !needle || !*needle)
		return FALSE;
	while (*haystack != 0) {
		if (Text_StartsWith(haystack, needle))
			return TRUE;
		haystack++;
	}
	return FALSE;
}

BOOL Text_EqualsNoCase(const CHAR *a, DWORD aLen, const CHAR *b)
{
	DWORD i = 0;
	while (i < aLen) {
		if (b[i] == 0 || Text_LowerAscii(a[i]) != Text_LowerAscii(b[i]))
			return FALSE;
		i++;
	}
	return b[i] == 0;
}
