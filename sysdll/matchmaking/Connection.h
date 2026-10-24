#pragma once
#include "../stdafx.h"

/* One TCP connection to the matchmaking service. Loader thread only. */

/* FALSE when it cannot (logged). */
BOOL Connection_Open();
BOOL Connection_IsOpen();
VOID Connection_Close();
/* After a socket fault: forget without touching it again. */
VOID Connection_Forget();

/* FALSE on send failure or oversize payload. */
BOOL Connection_Send(BYTE type, const BYTE *payload, DWORD len);

/* Server push that can arrive while a response is awaited. */
typedef VOID (*MmAnnounceHandler)(const BYTE *payload, DWORD len);

/* TRUE when the reply has responseType; ANNOUNCEs go to onAnnounce. */
BOOL Connection_Request(BYTE requestType, BYTE responseType, const BYTE *payload, DWORD len,
	BYTE *response, DWORD responseCap, DWORD *responseLen, MmAnnounceHandler onAnnounce);
/* TRUE when the server refused; clears it. */
BOOL Connection_TakeRejection(BYTE *reason, BYTE *serverVersion);
