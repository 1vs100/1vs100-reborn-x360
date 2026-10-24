#include "TitleLifecycle.h"
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
	ReportPatchPhase("end", "early");
	ReleaseLockBarrier();
	InterlockedExchange(&g_EarlyApplied, 1);
}

/* Title threads wait for the early rows only, never the whole pass. */
VOID ApplyEarlyTitlePatches()
{
	if (!TryBeginPass()) {
		DWORD waited = 0;
		while (g_PassInProgress.v != 0 && !g_EarlyApplied) {
			Sleep(1);
			++waited;
		}
		LOG_INFO("title", "early title patches: applied by the running pass (waited %u ms)",
			waited);
		return;
	}
	__try {
		/* No config: refuse, like the title pass. */
		if (SelectTitleProfile() && RevivalConfigReceived())
			ApplyEarlyRows();
	} __finally {
		EndPass();
	}
}

const char *TakeTitlePatchStep()
{
	return TakeCurrentPatchId();
}

static BOOL RunTitlePass()
{
	/* Detection and profile binding; nothing below runs unless supported. */
	if (!SelectTitleProfile()) {
		LOG_INFO("title", "title patches refused: unsupported build");
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
	if (!TryBeginPass()) {
		LOG_INFO("title", "title patches: another pass in progress, deferred");
		return APPLY_BUSY;
	}
	__try {
		result = RunTitlePass() ? APPLY_APPLIED : APPLY_UNSUPPORTED;
	} __finally {
		EndPass();
	}
	return result;
}

VOID ResetTitleState(DWORD exitedTitleId)
{
	/* Flush the exiting title lines now. */
	Log_RequestFlush();
	InterlockedExchange(&g_EarlyApplied, 0);
	ClearTitleProfile();
	ReleaseTitleCaves(exitedTitleId);
	TeardownMatchmakingClient();
	AvatarCache_Reset();
	ResetPatches();
}
