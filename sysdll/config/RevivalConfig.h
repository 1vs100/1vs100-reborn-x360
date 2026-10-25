#pragma once
#include "../stdafx.h"
#include "LaunchHandover.h"

/* Backend settings from the launcher. Until received they are 0 and the title pass refuses. */
extern DWORD g_RevivalInAddr;
extern DWORD g_RevivalSbgPort;
extern DWORD g_RevivalMatchmakingPort;
extern CHAR g_RevivalHost[REVIVAL_HOST_BYTES]; /* host= as written */

BOOL RevivalConfigReceived();

/* Launcher thread. FALSE on size mismatch. */
extern "C" BOOL SetRevivalConfig(const RevivalLaunchConfig *config);

/* Launcher lines after hand-over, into the same log. */
extern "C" VOID RevivalLogLine(DWORD level, const CHAR *line);
