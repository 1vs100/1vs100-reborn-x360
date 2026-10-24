#pragma once
#include "../platform/Types.h"

/* Types and limits shared by the API and the pure protocol code. */

/* A group holds four players; this console at least one. */
#define MATCHMAKING_MAX_REMOTE_PEERS 3
/* JOIN players, user 0 first. */
#define MATCHMAKING_MAX_LOCAL_PLAYERS 4

struct MatchmakingPeer {
	ULONGLONG xuid;
	WCHAR gamertag[32];
};

struct MatchmakingLocalPlayer {
	ULONGLONG xuid;
	CHAR gamertag[32]; /* at most 31 chars */
	DWORD user;        /* XAM index 0-3, not sent */
};

/* Relayed so consoles build each other avatars without a QNet data plane. */
#define AVATAR_METADATA_BYTES 1000

/* Relayed title events. */
#define MATCHMAKING_RELAY_MAX_BYTES 8192
/* Empty list means everyone. */
#define MATCHMAKING_RELAY_MAX_TARGETS 4
