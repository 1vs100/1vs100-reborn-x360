#include "TitleEntryHook.h"
#include "TitleLifecycle.h"
#include "../registry/PatchHelpers.h"
#include "../platform/HookCave.h"
#include "../platform/Log.h"

#define TITLE_PASS_WAIT_MS 15000

typedef DWORD (*TitleEntry_t)(DWORD, DWORD, DWORD, DWORD);

/* Original entry: the first instruction, then a branch back. NULL until hooked. */
static TitleEntry_t volatile g_EntryTramp = NULL;
static DWORD g_EntryAddr = 0;

/* Title thread, before any game code. Arguments pass through untouched. */
static DWORD TitleEntryHook(DWORD a, DWORD b, DWORD c, DWORD d)
{
	DWORD waited;
	LOG_INFO("core", "title entry %08X reached", g_EntryAddr);
	if (WaitForTitlePass(TITLE_PASS_WAIT_MS, &waited))
		LOG_INFO("core", "title entry: title pass done (waited %u ms), entering game", waited);
	else
		LOG_WARN("core", "title entry: title pass not done after %u ms, entering anyway",
			TITLE_PASS_WAIT_MS);
	return g_EntryTramp(a, b, c, d);
}

/* Only the first instruction moves; a relative branch there would land elsewhere. */
static BOOL MovableInstruction(DWORD word)
{
	DWORD op = word >> 26;
	return op != 16 && op != 18;
}

VOID InstallTitleEntryHook()
{
	PLDR_DATA_TABLE_ENTRY title = (PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle;
	DWORD entry;
	DWORD tramp = 0;

	if (title == NULL || title->EntryPoint == NULL) {
		ReportPatch("title entry wait", "skipped", 0, "site");
		return;
	}
	entry = (DWORD)title->EntryPoint;
	if (g_EntryTramp != NULL && g_EntryAddr == entry && HookCave_IsHooked(entry)) {
		ReportPatch("title entry wait", "already", entry, NULL);
		return;
	}
	if (!MovableInstruction(*(volatile DWORD *)entry)) {
		LOG_INFO("core", "title entry %08X starts with a relative branch (%08X)", entry,
			*(volatile DWORD *)entry);
		ReportPatch("title entry wait", "refused", entry, "signature");
		return;
	}
	g_EntryAddr = entry;
	if (!HookXamInternal(entry, (DWORD)TitleEntryHook, &tramp, "title entry")) {
		ReportPatch("title entry wait", "failed", entry, "hook");
		return;
	}
	InterlockedExchangePointer((PVOID volatile *)&g_EntryTramp, (PVOID)tramp);
	ReportPatch("title entry wait", "installed", entry, NULL);
}

VOID ResetTitleEntryHook()
{
	InterlockedExchangePointer((PVOID volatile *)&g_EntryTramp, NULL);
	g_EntryAddr = 0;
}
