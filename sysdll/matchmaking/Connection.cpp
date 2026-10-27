#include "Connection.h"
#include "protocol/Protocol.h"
#include "../config/RevivalConfig.h"
#include "../platform/Log.h"

#define MM_SOCKET_TIMEOUT_MS 3000
/* Relay payload plus header fields. */
#define MM_SEND_FRAME (MATCHMAKING_RELAY_MAX_BYTES + 256)

static SOCKET g_Socket = INVALID_SOCKET;
/* 0 unless the server refused the last request. */
static BYTE g_RejectReason = 0;
static BYTE g_RejectVersion = 0;

/* Usable outside Live. Never SO_PRIVATE: it confines the socket to the local subnet, and a
   routed backend then gets WSAEHOSTUNREACH. */
static VOID MarkInsecure(SOCKET s)
{
	BOOL insecure = TRUE;
	setsockopt(s, SOL_SOCKET, SO_MARKINSECURE, (const CHAR *)&insecure, sizeof(insecure));
}

BOOL Connection_Open()
{
	SOCKET s;
	SOCKADDR_IN addr;
	DWORD timeout = MM_SOCKET_TIMEOUT_MS;

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) {
		LOG_INFO("mm", "Matchmaking: socket() failed, WSAGetLastError=%d", WSAGetLastError());
		return FALSE;
	}
	MarkInsecure(s);
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const CHAR *)&timeout, sizeof(timeout));
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const CHAR *)&timeout, sizeof(timeout));

	ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.S_un.S_addr = g_RevivalInAddr;
	addr.sin_port = htons((WORD)g_RevivalMatchmakingPort);
	if (connect(s, (SOCKADDR *)&addr, sizeof(addr)) != 0) {
		LOG_INFO("mm", "Matchmaking: connect() to %08X:%u failed, WSAGetLastError=%d",
			(DWORD)g_RevivalInAddr, (unsigned)g_RevivalMatchmakingPort, WSAGetLastError());
		closesocket(s);
		return FALSE;
	}
	g_Socket = s;
	return TRUE;
}

BOOL Connection_IsOpen()
{
	return g_Socket != INVALID_SOCKET;
}

VOID Connection_Close()
{
	if (g_Socket != INVALID_SOCKET) {
		closesocket(g_Socket);
		g_Socket = INVALID_SOCKET;
	}
}

VOID Connection_Forget()
{
	g_Socket = INVALID_SOCKET;
}

static BOOL SendAll(const BYTE *data, DWORD len)
{
	DWORD sent = 0;
	while (sent < len) {
		int n = send(g_Socket, (const CHAR *)data + sent, len - sent, 0);
		if (n <= 0)
			return FALSE;
		sent += (DWORD)n;
	}
	return TRUE;
}

static BOOL RecvAll(BYTE *out, DWORD len)
{
	DWORD got = 0;
	while (got < len) {
		int n = recv(g_Socket, (CHAR *)out + got, len - got, 0);
		if (n <= 0)
			return FALSE;
		got += (DWORD)n;
	}
	return TRUE;
}

/* Another version is not read past its header. */
static BOOL RecvFrame(BYTE *type, BYTE *payload, DWORD payloadCap, DWORD *payloadLen)
{
	BYTE header[MM_HEADER_SIZE];
	BYTE version;
	uint32_t len;
	if (!RecvAll(header, MM_HEADER_SIZE))
		return FALSE;
	MmParseHeader(header, &version, type, &len);
	*payloadLen = len;
	if (version != MM_PROTOCOL_VERSION) {
		g_RejectReason = MM_REJECT_VERSION;
		g_RejectVersion = version;
		return FALSE;
	}
	if (*payloadLen > payloadCap)
		return FALSE;
	return RecvAll(payload, *payloadLen);
}

BOOL Connection_Send(BYTE type, const BYTE *payload, DWORD len)
{
	static BYTE frame[MM_SEND_FRAME];
	DWORD frameLen = MmFrame(frame, sizeof(frame), type, payload, len);
	return frameLen != 0 && SendAll(frame, frameLen);
}

BOOL Connection_Request(BYTE requestType, BYTE responseType, const BYTE *payload, DWORD len,
	BYTE *response, DWORD responseCap, DWORD *responseLen, MmAnnounceHandler onAnnounce)
{
	BYTE actualType;
	g_RejectReason = 0;
	if (!Connection_Send(requestType, payload, len))
		return FALSE;
	for (;;) {
		if (!RecvFrame(&actualType, response, responseCap, responseLen))
			return FALSE;
		if (actualType == MM_TYPE_REJECT) {
			g_RejectReason = *responseLen >= 1 ? response[0] : 0xFF;
			g_RejectVersion = MM_PROTOCOL_VERSION;
			return FALSE;
		}
		if (actualType != MM_TYPE_ANNOUNCE)
			return actualType == responseType;
		onAnnounce(response, *responseLen);
	}
}

BOOL Connection_TakeRejection(BYTE *reason, BYTE *serverVersion)
{
	if (g_RejectReason == 0)
		return FALSE;
	*reason = g_RejectReason;
	*serverVersion = g_RejectVersion;
	g_RejectReason = 0;
	return TRUE;
}
