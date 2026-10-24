#include "Protocol.h"
#include "../platform/Text.h"

VOID MmWriter_Init(MmWriter *w, BYTE *buf, DWORD cap)
{
	w->start = buf;
	w->p = buf;
	w->end = buf + cap;
	w->overflow = FALSE;
}

DWORD MmWriter_Length(const MmWriter *w)
{
	return (DWORD)(w->p - w->start);
}

VOID MmPutBytes(MmWriter *w, const BYTE *data, DWORD len)
{
	DWORD i = 0;
	if (w->overflow || (DWORD)(w->end - w->p) < len) {
		w->overflow = TRUE;
		return;
	}
	while (i < len) {
		w->p[i] = data[i];
		i++;
	}
	w->p += len;
}

VOID MmPutU8(MmWriter *w, BYTE v)
{
	MmPutBytes(w, &v, 1);
}

VOID MmPutU16(MmWriter *w, DWORD v)
{
	BYTE b[2];
	b[0] = (BYTE)(v >> 8);
	b[1] = (BYTE)v;
	MmPutBytes(w, b, 2);
}

VOID MmPutU32(MmWriter *w, DWORD v)
{
	BYTE b[4];
	b[0] = (BYTE)(v >> 24);
	b[1] = (BYTE)(v >> 16);
	b[2] = (BYTE)(v >> 8);
	b[3] = (BYTE)v;
	MmPutBytes(w, b, 4);
}

VOID MmPutU64(MmWriter *w, ULONGLONG v)
{
	MmPutU32(w, (DWORD)(v >> 32));
	MmPutU32(w, (DWORD)v);
}

VOID MmPutStr(MmWriter *w, const CHAR *s)
{
	DWORD len = Text_Length(s, 0xFFFF);
	MmPutU16(w, len);
	MmPutBytes(w, (const BYTE *)s, len);
}

VOID MmEncodeJoin(MmWriter *w, const MatchmakingLocalPlayer *players, DWORD count)
{
	DWORD i = 0;
	MmPutU8(w, (BYTE)count);
	while (i < count) {
		MmPutU64(w, players[i].xuid);
		MmPutStr(w, players[i].gamertag);
		i++;
	}
}

VOID MmEncodeXuid(MmWriter *w, ULONGLONG xuid)
{
	MmPutU64(w, xuid);
}

VOID MmEncodeAvatarPut(MmWriter *w, ULONGLONG xuid, const BYTE *blob)
{
	MmPutU64(w, xuid);
	MmPutU16(w, AVATAR_METADATA_BYTES);
	MmPutBytes(w, blob, AVATAR_METADATA_BYTES);
}

VOID MmEncodeAvatarGet(MmWriter *w, ULONGLONG peer)
{
	MmPutU64(w, peer);
}

VOID MmEncodeRelayPut(MmWriter *w, ULONGLONG xuid, const ULONGLONG *targets, DWORD targetCount,
	const BYTE *data, DWORD len)
{
	DWORD i = 0;
	MmPutU64(w, xuid);
	MmPutU16(w, targetCount);
	while (i < targetCount) {
		MmPutU64(w, targets[i]);
		i++;
	}
	MmPutU16(w, len);
	MmPutBytes(w, data, len);
}

DWORD MmFrame(BYTE *out, DWORD cap, BYTE type, const BYTE *payload, DWORD len)
{
	MmWriter w;
	MmWriter_Init(&w, out, cap);
	MmPutU8(&w, MM_PROTOCOL_VERSION);
	MmPutU8(&w, type);
	MmPutU32(&w, len);
	MmPutBytes(&w, payload, len);
	return w.overflow ? 0 : MmWriter_Length(&w);
}

VOID MmParseHeader(const BYTE *header, BYTE *version, BYTE *type, DWORD *payloadLen)
{
	*version = header[0];
	*type = header[1];
	*payloadLen = MmGetU32(header + 2);
}

WORD MmGetU16(const BYTE *p)
{
	return (WORD)(((DWORD)p[0] << 8) | (DWORD)p[1]);
}

DWORD MmGetU32(const BYTE *p)
{
	return ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | (DWORD)p[3];
}

ULONGLONG MmGetU64(const BYTE *p)
{
	return ((ULONGLONG)MmGetU32(p) << 32) | MmGetU32(p + 4);
}

/* Single-byte chars, at most 31 kept. */
static VOID CopyGamertag(WCHAR *out, const BYTE *in, DWORD len)
{
	DWORD i = 0;
	if (len >= 32)
		len = 31;
	while (i < len) {
		out[i] = (WCHAR)in[i];
		i++;
	}
	out[len] = 0;
}

BOOL MmDecodeRoster(const BYTE *p, DWORD n, ULONGLONG self, MatchmakingPeer *out, DWORD cap,
	DWORD *count, DWORD *groupId)
{
	DWORD pos = 2, kept = 0;
	DWORD consoles;
	if (n < 2)
		return FALSE;
	consoles = MmGetU16(p);
	while (consoles != 0) {
		DWORD players, consoleStart = kept;
		BOOL isSelf = FALSE;
		consoles--;
		if (pos + 1 > n)
			return FALSE;
		players = p[pos];
		pos++;
		if (players == 0 || players > MATCHMAKING_MAX_LOCAL_PLAYERS)
			return FALSE;
		/* Kept while there is room; this console taken back out once read. */
		while (players != 0) {
			ULONGLONG xuid;
			DWORD nameLen;
			players--;
			if (pos + 10 > n)
				return FALSE;
			xuid = MmGetU64(p + pos);
			nameLen = MmGetU16(p + pos + 8);
			pos += 10;
			if (pos + nameLen > n)
				return FALSE;
			if (xuid != 0 && xuid == self)
				isSelf = TRUE;
			if (kept < cap) {
				out[kept].xuid = xuid;
				CopyGamertag(out[kept].gamertag, p + pos, nameLen);
				kept++;
			}
			pos += nameLen;
		}
		if (isSelf)
			kept = consoleStart;
	}
	if (pos + 4 > n)
		return FALSE;
	*groupId = MmGetU32(p + pos);
	*count = kept;
	return TRUE;
}

BOOL MmDecodeJoinAck(const BYTE *p, DWORD n, DWORD *population, DWORD *groupId)
{
	if (n < 8)
		return FALSE;
	*population = MmGetU32(p);
	*groupId = MmGetU32(p + 4);
	return TRUE;
}

DWORD MmDecodePopulation(const BYTE *p, DWORD n)
{
	return n >= 4 ? MmGetU32(p) : 0;
}

BOOL MmDecodeAvatarReply(const BYTE *p, DWORD n, const BYTE **blob)
{
	if (n < 3 + AVATAR_METADATA_BYTES || p[0] != 1 || MmGetU16(p + 1) != AVATAR_METADATA_BYTES)
		return FALSE;
	*blob = p + 3;
	return TRUE;
}

DWORD MmDecodeRelayReply(const BYTE *p, DWORD n, MmRelayVisitor visit, VOID *ctx)
{
	DWORD pos = 2, visited = 0, count;
	if (n < 2)
		return 0;
	count = MmGetU16(p);
	while (count != 0) {
		ULONGLONG from;
		DWORD len;
		count--;
		if (pos + 10 > n)
			break;
		from = MmGetU64(p + pos);
		len = MmGetU16(p + pos + 8);
		pos += 10;
		if (pos + len > n || len > MATCHMAKING_RELAY_MAX_BYTES)
			break;
		visit(ctx, from, p + pos, len);
		visited++;
		pos += len;
	}
	return visited;
}
