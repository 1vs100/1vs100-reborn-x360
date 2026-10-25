#pragma once
#include "../platform/Types.h"
#include "protocol/MatchmakingTypes.h"

/* Relayed QNet payloads in two rings. Out: title threads queue, loader sends. In: loader queues, title main thread takes. */

struct RelayOutItem {
	ULONGLONG from; /* sending local player; 0 is unknown */
	DWORD targetCount;
	ULONGLONG targets[MATCHMAKING_RELAY_MAX_TARGETS];
	DWORD len;
	BYTE data[MATCHMAKING_RELAY_MAX_BYTES];
};

/* FALSE when full or out of bounds. */
BOOL RelayQueues_PushOut(
	ULONGLONG from, const ULONGLONG *targets, DWORD targetCount, const BYTE *data, DWORD len);
/* Oldest outgoing item; FALSE when none. */
BOOL RelayQueues_PopOut(RelayOutItem *item);
/* Full in ring drops. */
VOID RelayQueues_PushIn(ULONGLONG from, const BYTE *data, DWORD len);
/* Oldest incoming payload; FALSE when none, or too big (dropped). */
BOOL RelayQueues_PopIn(ULONGLONG *from, BYTE *out, DWORD capacity, DWORD *len);
VOID RelayQueues_Reset();
