#pragma once
#include <xtl.h>
#include "../sysdll/config/LaunchHandover.h"

/* Until LauncherLog_Open, lines go to the debug channel and are kept for the file. */
void Log(const CHAR *fmt, ...);
/* run-NNNNNN.log in logDir below the HDD root, deleting the oldest past logKeep. Early lines go in first. */
VOID LauncherLog_Open(RevivalLaunchConfig *config, const CHAR *logDir, DWORD logKeep);
/* Bytes in the log file so far. */
DWORD LauncherLog_FileBytes();
/* From here on lines go through the sysdll, into the same file. */
VOID LauncherLog_ThroughSysdll(RevivalLogLine_t logLine);
