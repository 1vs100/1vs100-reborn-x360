#include "Ini.h"

static BOOL IsBlank(CHAR c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

BOOL Ini_Next(const CHAR *text, DWORD len, DWORD *pos, IniLine *line)
{
	DWORD i = *pos;
	while (i < len) {
		DWORD ls = i, le, eq, ke, vs;
		while (i < len) {
			if (text[i] == '\n')
				break;
			i++;
		}
		le = i;
		if (i < len)
			i++;
		while (ls < le && IsBlank(text[ls]))
			ls++;
		while (le > ls && IsBlank(text[le - 1]))
			le--;
		if (ls == le || text[ls] == ';' || text[ls] == '#' || text[ls] == '[')
			continue;
		eq = ls;
		while (eq < le) {
			if (text[eq] == '=')
				break;
			eq++;
		}
		if (eq == le)
			continue;
		ke = eq;
		while (ke > ls && IsBlank(text[ke - 1]))
			ke--;
		vs = eq + 1;
		while (vs < le && IsBlank(text[vs]))
			vs++;
		line->key = text + ls;
		line->keyLen = ke - ls;
		line->value = text + vs;
		line->valueLen = le - vs;
		*pos = i;
		return TRUE;
	}
	*pos = i;
	return FALSE;
}

BOOL Ini_ParsePort(const CHAR *v, DWORD n, DWORD *out)
{
	DWORD value = 0, i = 0;
	if (n == 0)
		return FALSE;
	while (i < n) {
		if (v[i] < '0' || v[i] > '9')
			return FALSE;
		value = value * 10 + (DWORD)(v[i] - '0');
		if (value > 65535)
			return FALSE;
		i++;
	}
	if (value == 0)
		return FALSE;
	*out = value;
	return TRUE;
}

BOOL Ini_ParseIPv4(const CHAR *v, DWORD n, DWORD *out)
{
	DWORD addr = 0, part = 0, dots = 0, digits = 0, i = 0;
	while (i <= n) {
		if (i < n && v[i] >= '0' && v[i] <= '9') {
			part = part * 10 + (DWORD)(v[i] - '0');
			digits++;
			if (digits > 3 || part > 255)
				return FALSE;
		} else if (i == n || v[i] == '.') {
			if (!digits)
				return FALSE;
			addr = (addr << 8) | part;
			part = digits = 0;
			if (i < n)
				dots++;
		} else {
			return FALSE;
		}
		i++;
	}
	if (dots != 3)
		return FALSE;
	*out = addr;
	return TRUE;
}

BOOL Ini_IsHostname(const CHAR *v, DWORD n)
{
	DWORD i, label = 0;
	BOOL letter = FALSE;
	if (n == 0)
		return FALSE;
	for (i = 0; i < n; i++) {
		CHAR c = v[i];
		if (c == '.') {
			if (label == 0 || v[i - 1] == '-')
				return FALSE;
			label = 0;
			continue;
		}
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
			letter = TRUE;
		else if (c == '-' && label == 0)
			return FALSE;
		else if (c != '-' && (c < '0' || c > '9'))
			return FALSE;
		label++;
	}
	return label != 0 && v[n - 1] != '-' && letter;
}
