#pragma once
#include "../stdafx.h"
#include "protocol/MatchmakingTypes.h"

/* Avatar metadata for the relay: own (XAM warm-up) and roster peers. XAvatarInitialize can lag detection, so reads retry without waiting; loader thread only. */

/* Start the warm-up over. */
VOID AvatarCache_StartWarmup();
/* One warm-up step when due. */
VOID AvatarCache_Tick();
/* Forget the metadata. */
VOID AvatarCache_Reset();

/* Copy of a local user blob; FALSE until read. Users 1-3 read on demand after user 0. */
BOOL AvatarGetLocalMetadata(DWORD user, BYTE *out);

/* Peer metadata, one slot per peer; guarded by one SpinLock. */
BOOL AvatarCache_HasPeer(ULONGLONG xuid);
/* Into the peer slot, else a free one. */
VOID AvatarCache_StorePeer(ULONGLONG xuid, const BYTE *blob);
BOOL AvatarCache_GetPeer(ULONGLONG xuid, BYTE *out);
/* Forget blobs not on the roster. */
VOID AvatarCache_ForgetAbsentPeers(const MatchmakingPeer *peers, DWORD count);
VOID AvatarCache_ClearPeers();
