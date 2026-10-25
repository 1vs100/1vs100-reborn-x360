#include "Ini.h"

static BOOL IsBlank(CHAR c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

VOID Ini_Open(IniReader *reader, const CHAR *text, DWORD len)
{
	reader->text = text;
	reader->len = len;
	reader->pos = 0;
	reader->section = text;
	reader->sectionLen = 0;
}

static VOID SetSection(IniReader *reader, DWORD start, DWORD end)
{
	const CHAR *text = reader->text;
	if (end > start && text[end - 1] == ']')
		end--;
	while (start < end && IsBlank(text[start]))
		start++;
	while (end > start && IsBlank(text[end - 1]))
		end--;
	reader->section = text + start;
	reader->sectionLen = end - start;
}

BOOL Ini_Next(IniReader *reader, IniLine *line)
{
	const CHAR *text = reader->text;
	DWORD len = reader->len, i = reader->pos;
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
		if (ls == le || text[ls] == ';' || text[ls] == '#')
			continue;
		if (text[ls] == '[') {
			SetSection(reader, ls + 1, le);
			continue;
		}
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
		line->section = reader->section;
		line->sectionLen = reader->sectionLen;
		line->key = text + ls;
		line->keyLen = ke - ls;
		line->value = text + vs;
		line->valueLen = le - vs;
		reader->pos = i;
		return TRUE;
	}
	reader->pos = i;
	return FALSE;
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
