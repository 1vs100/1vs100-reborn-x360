#include "Format.h"

/* No CRT: runs on title threads. Loops stay explicit for Xenon codegen. */

void QuizFmtUint(
	CHAR *out, DWORD *pos, DWORD cap, DWORD value, int base, int upper, int width, char pad)
{
	char digits[12];
	int n = 0;
	do {
		int d = (int)(value % (DWORD)base);
		value /= (DWORD)base;
		digits[n++] = (char)((d < 10) ? ('0' + d) : ((upper ? 'A' : 'a') + (d - 10)));
	} while (value != 0 && n < (int)sizeof(digits));
	while (n < width && *pos + 1 < cap) {
		out[(*pos)++] = pad;
		width--;
	}
	while (n > 0 && *pos + 1 < cap)
		out[(*pos)++] = digits[--n];
}

static void QuizFmtInt(CHAR *out, DWORD *pos, DWORD cap, LONG value, int width, char pad)
{
	DWORD uval;
	if (value < 0) {
		if (*pos + 1 < cap)
			out[(*pos)++] = '-';
		uval = (DWORD)(-value);
		if (width > 0)
			width--;
	} else {
		uval = (DWORD)value;
	}
	QuizFmtUint(out, pos, cap, uval, 10, 0, width, pad);
}

/* Caller may prefix the line before this runs. */
VOID QuizFormatInto(CHAR *buf, DWORD *posPtr, DWORD cap, const CHAR *fmt, va_list args)
{
	DWORD pos = *posPtr;

	while (*fmt != 0 && pos + 1 < cap) {
		char c = *fmt++;
		if (c != '%') {
			buf[pos++] = c;
			continue;
		}

		BOOL leftJustify = FALSE;
		char pad = ' ';
		int width = 0;

		for (;;) {
			if (*fmt == '-') {
				leftJustify = TRUE;
				fmt++;
			} else if (*fmt == '0') {
				pad = '0';
				fmt++;
			} else
				break;
		}
		while (*fmt >= '0' && *fmt <= '9') {
			width = width * 10 + (*fmt - '0');
			fmt++;
		}
		/* 'l'/'ll' discarded: long and int are both 32 bits here. */
		if (*fmt == 'l') {
			fmt++;
			if (*fmt == 'l')
				fmt++;
		}

		char conv = *fmt;
		DWORD start = pos;
		if (conv != 0)
			fmt++;

		switch (conv) {
		case 'd':
		case 'i': {
			LONG v = va_arg(args, LONG);
			QuizFmtInt(buf, &pos, cap, v, leftJustify ? 0 : width, pad);
			break;
		}
		case 'u': {
			DWORD v = va_arg(args, DWORD);
			QuizFmtUint(buf, &pos, cap, v, 10, 0, leftJustify ? 0 : width, pad);
			break;
		}
		case 'x': {
			DWORD v = va_arg(args, DWORD);
			QuizFmtUint(buf, &pos, cap, v, 16, 0, leftJustify ? 0 : width, pad);
			break;
		}
		case 'X': {
			DWORD v = va_arg(args, DWORD);
			QuizFmtUint(buf, &pos, cap, v, 16, 1, leftJustify ? 0 : width, pad);
			break;
		}
		case 'p': {
			DWORD v = (DWORD)(DWORD_PTR)va_arg(args, PVOID);
			QuizFmtUint(buf, &pos, cap, v, 16, 1, 8, '0');
			break;
		}
		case 's': {
			const CHAR *s = va_arg(args, const CHAR *);
			int len = 0;
			const CHAR *p;
			if (!s)
				s = "(null)";
			for (p = s; *p; p++)
				len++;
			if (!leftJustify)
				while (len < width && pos + 1 < cap) {
					buf[pos++] = ' ';
					width--;
				}
			while (*s && pos + 1 < cap)
				buf[pos++] = *s++;
			break;
		}
		case 'c': {
			int ch = va_arg(args, int);
			if (pos + 1 < cap)
				buf[pos++] = (CHAR)ch;
			break;
		}
		case '%':
			if (pos + 1 < cap)
				buf[pos++] = '%';
			break;
		default:
			if (pos + 1 < cap)
				buf[pos++] = '%';
			if (conv != 0 && pos + 1 < cap)
				buf[pos++] = conv;
			break;
		}
		if (leftJustify) {
			while (pos - start < (DWORD)width && pos + 1 < cap)
				buf[pos++] = ' ';
		}
	}

	*posPtr = pos;
}
