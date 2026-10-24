#pragma once
#include "Types.h"

/* Title pump heartbeat and stall watchdog. Diagnostics builds only; production compiles both calls out. */
/* Pump counts per iteration; check runs on the loader poll thread, so it keeps running while the title is stuck. Tag "title". */

#ifdef LOADER_DIAGNOSTICS
VOID MainLoopWatchdog_Pump();
VOID MainLoopWatchdog_Check();
#else
#define MainLoopWatchdog_Pump() ((void)0)
#define MainLoopWatchdog_Check() ((void)0)
#endif
