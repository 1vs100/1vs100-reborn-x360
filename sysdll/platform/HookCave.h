#pragma once
#include "../stdafx.h"

/* Hooks through 24-byte slots in a code cave inside xam.xex. The cave is a writable zero run found by scan; the final write goes through XBDM DmSetMemory. */

/* TRUE on success, *pOrig is the trampoline. No signature check: use HOOK_CHECKED. */
BOOL HookXamInternal(DWORD addr, DWORD hook, DWORD *pOrig, const char *name);

/* TRUE when addr holds a hook placed here; repeat installs report already from memory. */
BOOL HookCave_IsHooked(DWORD addr);

/* Zeroes the slots of an exited title; the next pass reuses the cave. */
VOID ReleaseTitleCaves(DWORD titleId);

/* Zeroes cave bytes written here, so the next load finds the same zero run. */
VOID ReleaseXamCaves();

/* Logged at the end of every title pass. */
VOID LogXamCaveUsage();
