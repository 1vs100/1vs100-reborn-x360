#pragma once
#include "Types.h"
#include <stdarg.h>

/* CRT-free printf subset for loader logging. 'l'/'ll' accepted and ignored. */

/* Formats into buf from *pos, never past cap - 1; does not NUL-terminate. */
VOID QuizFormatInto(CHAR *buf, DWORD *posPtr, DWORD cap, const CHAR *fmt, va_list args);

void QuizFmtUint(
	CHAR *out, DWORD *pos, DWORD cap, DWORD value, int base, int upper, int width, char pad);
