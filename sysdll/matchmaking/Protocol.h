#pragma once
#include "MatchmakingTypes.h"

/* Wire protocol as pure functions over byte buffers. Big-endian throughout. */

/* Every frame: version u8 | type u8 | length u32 | payload. Another version is not read further. */
#define MM_PROTOCOL_VERSION 2
#define MM_HEADER_SIZE 6

#define MM_TYPE_JOIN 0x01
#define MM_TYPE_JOIN_ACK 0x02
#define MM_TYPE_HEARTBEAT 0x03
#define MM_TYPE_HEARTBEAT_ACK 0x04
#define MM_TYPE_LEAVE 0x05
#define MM_TYPE_ROSTER 0x06
#define MM_TYPE_ROSTER_REPLY 0x07
#define MM_TYPE_ANNOUNCE 0x08
#define MM_TYPE_AVATAR_PUT 0x09
#define MM_TYPE_AVATAR_ACK 0x0A
#define MM_TYPE_AVATAR_GET 0x0B
#define MM_TYPE_AVATAR_REPLY 0x0C
#define MM_TYPE_RELAY_PUT 0x0D
#define MM_TYPE_RELAY_ACK 0x0E
#define MM_TYPE_RELAY_POLL 0x0F
#define MM_TYPE_RELAY_REPLY 0x10
#define MM_TYPE_REJECT 0x11

/* Server closes after REJECT. */
#define MM_REJECT_VERSION 1
#define MM_REJECT_MALFORMED_JOIN 2
#define MM_REJECT_UNKNOWN_TYPE 3

/* Puts bounds-check; past the end sets overflow. Caller checks once at the end. */
struct MmWriter {
	BYTE *start;
	BYTE *p;
	BYTE *end;
	BOOL overflow;
};
VOID MmWriter_Init(MmWriter *w, BYTE *buf, DWORD cap);
DWORD MmWriter_Length(const MmWriter *w);
VOID MmPutU8(MmWriter *w, BYTE v);
VOID MmPutU16(MmWriter *w, DWORD v);
VOID MmPutU32(MmWriter *w, DWORD v);
VOID MmPutU64(MmWriter *w, ULONGLONG v);
VOID MmPutBytes(MmWriter *w, const BYTE *data, DWORD len);
/* u16 length, no terminator. */
VOID MmPutStr(MmWriter *w, const CHAR *s);

/* No request names a session; the server keeps it with the connection. */
/* Players, user 0 first; the first XUID identifies the console. */
VOID MmEncodeJoin(MmWriter *w, const MatchmakingLocalPlayer *players, DWORD count);
/* First XUID alone. */
VOID MmEncodeXuid(MmWriter *w, ULONGLONG xuid);
VOID MmEncodeAvatarPut(MmWriter *w, ULONGLONG xuid, const BYTE *blob);
VOID MmEncodeAvatarGet(MmWriter *w, ULONGLONG peer);
VOID MmEncodeRelayPut(MmWriter *w, ULONGLONG xuid, const ULONGLONG *targets, DWORD targetCount,
	const BYTE *data, DWORD len);

/* 0 when it does not fit. */
DWORD MmFrame(BYTE *out, DWORD cap, BYTE type, const BYTE *payload, DWORD len);
/* Header fields. */
VOID MmParseHeader(const BYTE *header, BYTE *version, BYTE *type, DWORD *payloadLen);

WORD MmGetU16(const BYTE *p);
DWORD MmGetU32(const BYTE *p);
ULONGLONG MmGetU64(const BYTE *p);

/* Group consoles with players, then group id. Self console skipped; at most cap. FALSE when truncated or a console lists 0 or 5+ players. */
BOOL MmDecodeRoster(const BYTE *p, DWORD n, ULONGLONG self, MatchmakingPeer *out, DWORD cap,
	DWORD *count, DWORD *groupId);
/* Population and group id; FALSE when short. */
BOOL MmDecodeJoinAck(const BYTE *p, DWORD n, DWORD *population, DWORD *groupId);
/* Population, 0 when absent. */
DWORD MmDecodePopulation(const BYTE *p, DWORD n);
/* TRUE with *blob on a found, complete reply. */
BOOL MmDecodeAvatarReply(const BYTE *p, DWORD n, const BYTE **blob);
/* Visits each item in order; a bad item ends the reply. */
typedef VOID (*MmRelayVisitor)(VOID *ctx, ULONGLONG from, const BYTE *data, DWORD len);
DWORD MmDecodeRelayReply(const BYTE *p, DWORD n, MmRelayVisitor visit, VOID *ctx);
