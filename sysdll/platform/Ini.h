#pragma once
#include "Types.h"

/* CRT-free ini reader. */

struct IniReader {
	const CHAR *text;
	DWORD len;
	DWORD pos;
	const CHAR *section; /* not NUL-terminated; empty before the first [section] */
	DWORD sectionLen;
};

struct IniLine {
	const CHAR *section; /* not NUL-terminated */
	DWORD sectionLen;
	const CHAR *key; /* not NUL-terminated */
	DWORD keyLen;
	const CHAR *value; /* not NUL-terminated */
	DWORD valueLen;
};

VOID Ini_Open(IniReader *reader, const CHAR *text, DWORD len);
/* LF or CR LF ends; trims spaces and tabs. Skips blanks, ';'/'#' comments, lines without '='.
 * [name] starts a section. */
BOOL Ini_Next(IniReader *reader, IniLine *line);

/* Dotted quad, at most 3 digits a part, to 0xAABBCCDD. */
BOOL Ini_ParseIPv4(const CHAR *v, DWORD n, DWORD *out);
/* Needs a letter, so "1.2.3" is not a name. The caller bounds the length. */
BOOL Ini_IsHostname(const CHAR *v, DWORD n);
