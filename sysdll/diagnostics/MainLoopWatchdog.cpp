#include "../platform/MainLoopWatchdog.h"
#include "../platform/Log.h"

#define WATCHDOG_BEAT_MS 30000
#define WATCHDOG_STALL_MS 5000
#define WATCHDOG_REPORT_MS 10000

/* Pump writes, loader thread reads; a stale read only shifts a log line. */
static volatile LONG g_PumpCount = 0;
static volatile DWORD g_PumpLastTick = 0;
static volatile DWORD g_PumpThread = 0;

static DWORD g_LastCount = 0;
static DWORD g_BeatCount = 0;
static DWORD g_NextBeatTick = 0;
static DWORD g_NextReportTick = 0;
static BOOL g_Stalled = FALSE;
static BOOL g_Armed = FALSE;

VOID MainLoopWatchdog_Pump()
{
	if (g_PumpThread == 0) {
		g_PumpThread = (DWORD)GetCurrentThreadId();
		LOG_INFO("title", "main pump first call thread=%08X", g_PumpThread);
	}
	g_PumpLastTick = GetTickCount();
	InterlockedIncrement(&g_PumpCount);
}

VOID MainLoopWatchdog_Check()
{
	DWORD now = GetTickCount();
	DWORD count = (DWORD)g_PumpCount;
	DWORD idle;

	if (count == 0)
		return; /* not pumping yet */

	if (!g_Armed) {
		g_Armed = TRUE;
		g_BeatCount = count;
		g_NextBeatTick = now + WATCHDOG_BEAT_MS;
		g_LastCount = count;
	}

	if (count != g_LastCount) {
		if (g_Stalled) {
			LOG_INFO("title", "main pump moving again after stall, pumps=%u", count);
			g_Stalled = FALSE;
		}
		g_LastCount = count;
	} else {
		idle = now - g_PumpLastTick;
		if (idle >= WATCHDOG_STALL_MS) {
			if (!g_Stalled) {
				g_Stalled = TRUE;
				LOG_WARN("title", "main pump silent %u ms, pumps=%u thread=%08X", idle, count,
					g_PumpThread);
				g_NextReportTick = now + WATCHDOG_REPORT_MS;
			} else if ((LONG)(now - g_NextReportTick) >= 0) {
				LOG_WARN("title", "main pump still silent %u ms, pumps=%u thread=%08X", idle,
					count, g_PumpThread);
				g_NextReportTick = now + WATCHDOG_REPORT_MS;
			}
		}
	}

	if (!g_Stalled && (LONG)(now - g_NextBeatTick) >= 0) {
		LOG_INFO("title", "main pump beat pumps=%u (+%u in 30 s)", count, count - g_BeatCount);
		g_BeatCount = count;
		g_NextBeatTick = now + WATCHDOG_BEAT_MS;
	}
}
