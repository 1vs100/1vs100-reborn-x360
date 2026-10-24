#pragma once
#include "../stdafx.h"
#include "MatchmakingTypes.h"

/* TCP client for the gameserver matchmaking protocol. Separate from SBG, which reimplements the title wire protocol. */
VOID SetupMatchmakingClient();
VOID TeardownMatchmakingClient();
/* After a socket fault: drop and rejoin later. */
VOID MatchmakingAbandonConnection();
/* Once per loader poll; no-op until setup. Runs inline on the calling thread. */
VOID MatchmakingTick();
/* Leave the roster; the tick does the leave. Resume to rejoin. */
VOID MatchmakingPause();
VOID MatchmakingResume();

DWORD MatchmakingGetPeers(MatchmakingPeer *out, DWORD capacity);

/* Player users by XAM bit; user 0 identifies the console. A change rejoins. */
VOID MatchmakingSetPlayersInGame(DWORD userMask);

/* Cached peer metadata; FALSE when unfetched. */
BOOL MatchmakingGetPeerAvatar(ULONGLONG xuid, BYTE *out);

/* Relayed QNet payloads. No targets means all others; a non-local from goes out as user 0. */
BOOL MatchmakingRelaySend(
	ULONGLONG from, const ULONGLONG *targets, DWORD targetCount, const BYTE *data, DWORD len);
BOOL MatchmakingRelayReceive(ULONGLONG *from, BYTE *out, DWORD capacity, DWORD *len);
ULONGLONG MatchmakingSelfXuid();
