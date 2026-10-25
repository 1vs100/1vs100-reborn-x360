#include "../stdafx.h"
#include "RevivalConfig.h"
#include "PatchSwitches.h"
#include "../platform/Log.h"
#include "../platform/LogConfig.h"
#include "../platform/Text.h"

DWORD g_RevivalInAddr = 0;
DWORD g_RevivalSbgPort = 0;
DWORD g_RevivalMatchmakingPort = 0;
CHAR g_RevivalHost[REVIVAL_HOST_BYTES] = {0};
static volatile LONG g_Received = FALSE;

BOOL RevivalConfigReceived()
{
	return g_Received;
}

extern "C" BOOL SetRevivalConfig(const RevivalLaunchConfig *config)
{
	DWORD addr;

	if (config == NULL || config->size != sizeof(RevivalLaunchConfig)) {
		LOG_ERROR("cfg", "config from the launcher refused: size %u, this build expects %u",
			config ? config->size : 0, (DWORD)sizeof(RevivalLaunchConfig));
		return FALSE;
	}
	if (!LogConfig_BufferKbValid(config->logBufferKb)) {
		LOG_ERROR("cfg", "config from the launcher refused: log buffer %u KB", config->logBufferKb);
		return FALSE;
	}
	addr = config->backendInAddr;
	g_RevivalInAddr = addr;
	g_RevivalSbgPort = config->sbgPort;
	g_RevivalMatchmakingPort = config->matchmakingPort;
	SetPatchSwitches(config->patchSwitches);
	{
		LogSettings log;
		log.sink = config->logSink;
		log.levelSet = config->logLevelSet;
		log.level = config->logLevel;
		log.debugAlso = config->logDebugAlso;
		log.bufferKb = config->logBufferKb;
		log.flushMs = config->logFlushMs;
		log.file = config->logFile;
		log.fileBytes = config->logFileBytes;
		log.tags = config->logTags;
		Log_Configure(&log);
	}
	/* Publish before the flag. */
	Text_Append(g_RevivalHost, 0, sizeof(g_RevivalHost), config->backendHost);
	InterlockedExchange(&g_Received, TRUE);
	LOG_INFO("cfg", "config from the launcher: address=%s (%u.%u.%u.%u) sbg=%u mm=%u",
		g_RevivalHost, addr >> 24, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF,
		g_RevivalSbgPort, g_RevivalMatchmakingPort);
	return TRUE;
}

extern "C" VOID RevivalLogLine(const CHAR *line)
{
	Log_EmitText(LOG_LEVEL_INFO, "launcher", line);
}
