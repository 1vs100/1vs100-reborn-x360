#pragma once
#include "Types.h"

/* CRT-free key=value reader. */

struct IniLine {
	const CHAR *key; /* not NUL-terminated */
	DWORD keyLen;
	const CHAR *value; /* not NUL-terminated */
	DWORD valueLen;
};

/* LF or CR LF ends; trims spaces and tabs. Skips blanks, [sections], ';'/'#' comments, lines without '='. */
BOOL Ini_Next(const CHAR *text, DWORD len, DWORD *pos, IniLine *line);

/* 1-65535, digits only. */
BOOL Ini_ParsePort(const CHAR *v, DWORD n, DWORD *out);
/* Dotted quad, at most 3 digits a part, to 0xAABBCCDD. */
BOOL Ini_ParseIPv4(const CHAR *v, DWORD n, DWORD *out);
/* Needs a letter, so "1.2.3" is not a name. The caller bounds the length. */
BOOL Ini_IsHostname(const CHAR *v, DWORD n);
