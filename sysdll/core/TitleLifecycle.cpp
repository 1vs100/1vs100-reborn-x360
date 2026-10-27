#include "TitleLifecycle.h"
#include "TitleEntryHook.h"
#include "../registry/PatchHelpers.h"
#include "../registry/PatchRegistry.h"
#include "../config/PatchSwitches.h"
#include "../config/RevivalConfig.h"
#include "../platform/HookCave.h"
#include "../platform/Log.h"
#include "../platform/SpinLock.h"
#include "../matchmaking/MatchmakingClient.h"
#include "../matchmaking/AvatarCache.h"

/* One pass at a time. LoaderMain retries later; the title thread waits only for the early rows. */
static SpinLock g_PassInProgress = SPINLOCK_INIT;
/* Early rows applied for this launch. */
static volatile LONG g_EarlyApplied = 0;
/* Main pass finished for this launch, applied or refused. */
static volatile LONG g_TitlePassDone = 0;

static BOOL TryBeginPass()
{
	return SpinLock_TryLock(&g_PassInProgress);
}

static VOID EndPass()
{
	SpinLock_Unlock(&g_PassInProgress);
}

/* Inside a pass, with the profile selected and the config received. */
static VOID ApplyEarlyRows()
{
	if (g_EarlyApplied)
		return;
	LOG_INFO("title", "applying early title patches build=%s", TitleProfileName());
	ReportPatchPhase("begin", "early");
	ApplyPatchPhase(PHASE_EARLY);
	/* Entry held until the main pass is done: no game code runs before it. Cleared here, since
	   a refused pass of another title sets it. */
	InterlockedExchange(&g_TitlePassDone, 0);
	InstallTitleEntryHook();
	ReportPatchPhase("end", "early");
	ReleaseLockBarrier();
	InterlockedExchange(&g_EarlyApplied, 1);
}

/* Title threads wait for the early rows only, never the whole pass. */
/* Caller holds the pass. */
static VOID ApplyEarlyInPass()
{
	__try {
		/* No config: refuse, like the title pass. */
		if (SelectTitleProfile() && RevivalConfigReceived())
			ApplyEarlyRows();
	} __finally {
		EndPass();
	}
}

VOID ApplyEarlyTitlePatches()
{
	DWORD waited = 0;
	/* The running pass may be another title's, which applies no early rows: retry. */
	while (!TryBeginPass()) {
		while (g_PassInProgress.v != 0 && !g_EarlyApplied) {
			Sleep(1);
			++waited;
		}
		if (g_EarlyApplied) {
			LOG_INFO("title", "early title patches: applied by the running pass (waited %u ms)",
				waited);
			return;
		}
	}
	ApplyEarlyInPass();
}

VOID TryApplyEarlyTitlePatches()
{
	if (TryBeginPass())
		ApplyEarlyInPass();
}

const char *TakeTitlePatchStep()
{
	return TakeCurrentPatchId();
}

static BOOL RunTitlePass()
{
	/* Detection and profile binding; nothing below runs unless supported. */
	if (!SelectTitleProfile()) {
		ReportPatchPhase("unsupported", "title");
		return FALSE;
	}
	/* No backend address without the launcher config. */
	if (!RevivalConfigReceived()) {
		LOG_ERROR("title",
			"title patches refused: no config from the launcher (start the game with "
			"1vs100XdkLauncher)");
		ReportPatchPhase("unsupported", "title");
		ClearTitleProfile();
		return FALSE;
	}

	/* Early rows first: releases a waiting title thread. */
	ApplyEarlyRows();
	LOG_INFO("title", "applying title patches build=%s", TitleProfileName());
	ReportPatchPhase("begin", "title");
	ApplyPatchPhase(PHASE_TITLE);
	LogXamCaveUsage();
	ReportPatchPhase("end", "title");
	/* Not patches: arm per-title services; the loader tick drives them. */
	AvatarCache_StartWarmup();
	SetupMatchmakingClient();
	return TRUE;
}

ApplyResult ApplyTitlePatches()
{
	ApplyResult result = APPLY_UNSUPPORTED;
	/* The title id can change before xam maps the image: not ready yet, not unsupported. */
	if (*XexExecutableModuleHandle == NULL)
		return APPLY_BUSY;
	if (!TryBeginPass()) {
		LOG_INFO("title", "title patches: another pass in progress, deferred");
		return APPLY_BUSY;
	}
	__try {
		result = RunTitlePass() ? APPLY_APPLIED : APPLY_UNSUPPORTED;
	} __finally {
		InterlockedExchange(&g_TitlePassDone, 1);
		EndPass();
	}
	return result;
}

BOOL WaitForTitlePass(DWORD timeoutMs, DWORD *waitedMs)
{
	*waitedMs = 0;
	while (!g_TitlePassDone) {
		if (*waitedMs >= timeoutMs)
			return FALSE;
		Sleep(1);
		++*waitedMs;
	}
	return TRUE;
}

VOID ResetTitleState(DWORD exitedTitleId)
{
	/* Flush the exiting title lines now. */
	Log_RequestFlush();
	InterlockedExchange(&g_EarlyApplied, 0);
	InterlockedExchange(&g_TitlePassDone, 0);
	ResetTitleEntryHook();
	ClearTitleProfile();
	ReleaseTitleCaves(exitedTitleId);
	TeardownMatchmakingClient();
	AvatarCache_Reset();
	ResetPatches();
}
