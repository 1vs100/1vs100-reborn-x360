#pragma once
#include "Types.h"

/* CRT-free string helpers. No CRT on hook paths; loops use while + if/break for Xenon codegen. */
/* Append takes pos and cap, returns the new length; never writes past cap - 1. */

DWORD Text_Length(const CHAR *s, DWORD max);
DWORD Text_Append(CHAR *dst, DWORD pos, DWORD cap, const CHAR *src);
/* Up to maxChars characters; anything outside ASCII becomes '?'. */
DWORD Text_AppendWide(CHAR *dst, DWORD pos, DWORD cap, const WCHAR *src, DWORD maxChars);
DWORD Text_AppendHex(CHAR *dst, DWORD pos, DWORD cap, DWORD v, DWORD digits);
/* out needs 21 bytes. */
DWORD Text_FormatDecimalU64(ULONGLONG v, CHAR *out);

CHAR Text_LowerAscii(CHAR c);
BOOL Text_Contains(const CHAR *haystack, const CHAR *needle);
BOOL Text_StartsWith(const CHAR *text, const CHAR *prefix);
BOOL Text_EqualsNoCase(const CHAR *a, DWORD aLen, const CHAR *b);
