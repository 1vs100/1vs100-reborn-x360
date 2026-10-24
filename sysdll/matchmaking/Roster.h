#pragma once
#include "MatchmakingTypes.h"

/* Remote peers of this session; guarded by one SpinLock. */

VOID Roster_Set(const MatchmakingPeer *peers, DWORD count);
VOID Roster_Clear();
/* Copies at most capacity; returns how many. */
DWORD Roster_Get(MatchmakingPeer *out, DWORD capacity);
DWORD Roster_Count();
