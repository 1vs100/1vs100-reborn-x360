#pragma once
#include "../stdafx.h"
#include "../platform/SpinLock.h"

/* Content fallback helpers: list package files, cursor per enumerator, resolve device ids. */

struct ContentFallbackItem {
	CHAR fileName[42]; /* not NUL-terminated at 42 */
};
#define CONTENT_FALLBACK_MAX_ITEMS 24

/* Lists package files; returns how many. */
DWORD ScanContentDirectory(DWORD titleId, ContentFallbackItem *items, DWORD maxItems);

struct ContentEnumCursor {
	DWORD key;  /* 0 is free */
	DWORD next; /* next item to hand out */
	DWORD tick; /* last request tick; a long gap means recycled */
};
extern SpinLock g_ContentEnumLock; /* guards GetContentCursor */
ContentEnumCursor *GetContentCursor(DWORD key);
/* Forget every cursor. */
VOID ResetContentCursors();

BOOL ResolveCurrentTitleId(DWORD *pTitleId);
BOOL ResolveHddDeviceId(DWORD *pDeviceId);
