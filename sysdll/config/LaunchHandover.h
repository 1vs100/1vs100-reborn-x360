#pragma once
#include "../platform/Types.h"
#include "../platform/LogConfig.h"

/* Launcher to sysdll hand-over. `size` turns away a launcher and sysdll from different builds. */
#define REVIVAL_SET_CONFIG_ORDINAL 1
#define REVIVAL_LOG_LINE_ORDINAL 2
#define REVIVAL_PATCH_SWITCHES_BYTES 1024
/* Host as written may be at most this many chars ("http://" + host + "/grid/v1" + NUL in 32 bytes). */
#define REVIVAL_GRID_HOST_MAX 16
#define REVIVAL_HOST_BYTES (REVIVAL_GRID_HOST_MAX + 1)
/* sbg_port becomes a signed 16-bit li immediate. */
#define REVIVAL_SBG_PORT_MAX 32767

struct RevivalLaunchConfig {
	DWORD size;          /* sizeof(RevivalLaunchConfig) */
	DWORD backendInAddr; /* a.b.c.d as 0xAABBCCDD, already resolved */
	DWORD sbgPort;
	DWORD matchmakingPort;
	DWORD madsPort; /* Massive ad server */
	CHAR backendHost[REVIVAL_HOST_BYTES]; /* host= as written */
	/* 1vs100Reborn.disable as read; empty when there is none. Read by the launcher: the file may unmount with the game. */
	CHAR patchSwitches[REVIVAL_PATCH_SWITCHES_BYTES];
	/* [log] keys */
	DWORD logSink;      /* LOG_SINK_* bits */
	BOOL logLevelSet;   /* FALSE: the sysdll build's default */
	DWORD logLevel;     /* most verbose level kept */
	DWORD logDebugAlso; /* file-only sink: this level and above also to the debug channel */
	DWORD logBufferKb;  /* LogConfig_BufferKbValid */
	DWORD logFlushMs;
	/* This launch log file as a device path; empty when there is no file. Launcher wrote logFileBytes already. */
	DWORD logFileBytes;
	CHAR logFile[LOG_PATH_BYTES];
	CHAR logTags[LOG_TAGS_BYTES]; /* as written */
};

typedef BOOL (*SetRevivalConfig_t)(const RevivalLaunchConfig *config);
/* Launcher logs through the sysdll after hand-over, so its last lines land in the same file. */
typedef VOID (*RevivalLogLine_t)(DWORD level, const CHAR *line);
