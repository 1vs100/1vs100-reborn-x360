#pragma once
#include "../sysdll/config/LaunchHandover.h"

/* Ini text to settings. No XDK calls, so host tests build it. */

#define LAUNCHER_LOG_DIR_BYTES 96

struct LauncherConfig {
	RevivalLaunchConfig handover;
	BOOL hostIsName; /* handover.backendHost needs a DNS lookup */
	CHAR logDir[LAUNCHER_LOG_DIR_BYTES]; /* below the HDD root */
	DWORD logKeep;
};

typedef void (*ConfigWarn_t)(const CHAR *fmt, ...);

/* Defaults, then the ini text. A bad value keeps the previous one and goes to warn.
 * FALSE without a valid [backend] host. */
BOOL LauncherConfig_Parse(const CHAR *text, DWORD len, LauncherConfig *config, ConfigWarn_t warn);
