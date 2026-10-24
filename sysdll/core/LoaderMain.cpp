#include "../stdafx.h"
#include "KernelLoadHook.h"
#include "TitleLifecycle.h"
#include "../matchmaking/MatchmakingClient.h"
#include "../matchmaking/AvatarCache.h"
#include "../platform/HookCave.h"
#include "../platform/Log.h"
#include "../platform/MainLoopWatchdog.h"

volatile LONG g_LoopCompleted = FALSE;
volatile LONG g_Run = TRUE;
BOOL g_TitleLoaded = FALSE;
DWORD g_LastTitleId = 0xFFFFFFFFu;
DWORD g_ApplyAttempts = 0;
const DWORD kMaxApplyAttempts = 3;
const DWORD kLogShutdownWaitMs = 3000;
const DWORD kDetachWaitMs = 25000;

static void LoaderMain()
{
	DWORD titleId;
	DWORD lastAliveTick = 0;
	DWORD faultCount = 0, faultWindowStart = 0;

	SetupKernelLoadHook();
	LOG_INFO("core", "LoaderMain: polling, title=%08X", XamGetCurrentTitleId());

	while (g_Run) {
		__try {
			titleId = XamGetCurrentTitleId();
			if (titleId != g_LastTitleId) {
				if (g_TitleLoaded) {
					g_TitleLoaded = FALSE;
					ResetTitleState(g_LastTitleId);
				}
				if (++g_ApplyAttempts > kMaxApplyAttempts) {
					LOG_ERROR("core",
						"LoaderMain: title %08X patch pass faulted %u times, giving up", titleId,
						kMaxApplyAttempts);
					g_LastTitleId = titleId;
					g_ApplyAttempts = 0;
				} else {
					ApplyResult applied = ApplyTitlePatches();
					if (applied == APPLY_BUSY) {
						// Another pass is running; retry without counting an attempt.
						--g_ApplyAttempts;
					} else {
						g_LastTitleId = titleId;
						g_ApplyAttempts = 0;
						if (applied == APPLY_APPLIED) {
							LOG_INFO("core", "target title detected");
							g_TitleLoaded = TRUE;
						}
					}
				}
			}
			if (g_TitleLoaded) {
				AvatarCache_Tick();
				MatchmakingTick();
				MainLoopWatchdog_Check();
				Sleep(100);
			} else {
				Sleep(10);
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			/* A fault must not end the loop: drop matchmaking and rejoin; park after 5 faults in 60 s. */
			const char *step = TakeTitlePatchStep();
			DWORD now = GetTickCount();
			if (now - faultWindowStart > 60000) {
				faultWindowStart = now;
				faultCount = 0;
			}
			++faultCount;
			LOG_ERROR("core", "LoaderMain: exception %08X (#%u in 60s) in %s", GetExceptionCode(),
				faultCount, step ? step : "loader loop");
			if (faultCount > 5) {
				LOG_ERROR("core", "LoaderMain: too many faults, parking thread");
				break;
			}
			MatchmakingAbandonConnection();
		}
		if (GetTickCount() - lastAliveTick > 60000) {
			lastAliveTick = GetTickCount();
			LOG_INFO("core", "LoaderMain: alive, title=%08X", XamGetCurrentTitleId());
		}
	}

	InterlockedExchange(&g_LoopCompleted, TRUE);
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	(void)hModule;
	(void)lpReserved;
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		LOG_INFO("core", "DllMain DLL_PROCESS_ATTACH entered");
		LOG_INFO("core", "Plugin loaded");
		InterlockedExchange(&g_Run, TRUE);
		InterlockedExchange(&g_LoopCompleted, FALSE);
		{
			HANDLE thread;
			DWORD threadId;
			ExCreateThread(&thread, 0, &threadId, (PVOID)XapiThreadStartup,
				(LPTHREAD_START_ROUTINE)LoaderMain, NULL, 0x2);
			XSetThreadProcessor(thread, 4);
			SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
			ResumeThread(thread);
			CloseHandle(thread);
		}
		/* Writer thread: neither title threads nor the loop wait on disk. Idle until settings name a file. */
		{
			HANDLE thread;
			DWORD threadId;
			ExCreateThread(&thread, 0, &threadId, (PVOID)XapiThreadStartup,
				(LPTHREAD_START_ROUTINE)Log_WriterMain, NULL, 0x2);
			ResumeThread(thread);
			CloseHandle(thread);
		}
		break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		InterlockedExchange(&g_Run, FALSE);
		LOG_INFO("core", "Plugin detach, waiting for loop completion");
		/* Bounded: runs under the loader lock. */
		{
			DWORD start = GetTickCount();
			while (!g_LoopCompleted) {
				if (GetTickCount() - start > kDetachWaitMs) {
					LOG_WARN("core",
						"Plugin detach: loop still running after %u ms, unloading anyway",
						kDetachWaitMs);
					break;
				}
				Sleep(10);
			}
		}
		/* Exports go back, the cave is zeroed. Only the launcher unloads, as the running title. */
		RemoveKernelLoadHook();
		if (g_TitleLoaded)
			LOG_ERROR("core", "Plugin detach while title %08X is patched: cave left as is",
				g_LastTitleId);
		else
			ReleaseXamCaves();
		LOG_INFO("core", "bye");
		/* Last: flush what is buffered, stop the writer. */
		Log_Shutdown(kLogShutdownWaitMs);
		break;
	}

	return TRUE;
}
