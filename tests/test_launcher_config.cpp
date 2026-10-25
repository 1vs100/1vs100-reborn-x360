#include "check.h"
#include <string.h>
#include "../launcher/LauncherConfig.h"

static int g_Warnings;

static void Warn(const CHAR *fmt, ...)
{
	(void)fmt;
	g_Warnings++;
}

static void Parse(const char *text, LauncherConfig *config)
{
	g_Warnings = 0;
	LauncherConfig_Parse(text, (DWORD)strlen(text), config, Warn);
}

void TestLauncherConfig()
{
	LauncherConfig c;

	Parse("", &c);
	CHECK(g_Warnings == 0 && c.handover.size == sizeof(c.handover));
	CHECK(c.handover.sbgPort == 1000 && c.handover.matchmakingPort == 1001);
	CHECK(c.handover.logSink == LOG_SINK_FILE && !c.handover.logLevelSet);
	CHECK(c.handover.logDebugAlso == LOG_LEVEL_WARN && c.handover.logBufferKb == 2048);
	CHECK(strcmp(c.logDir, "1vs100Reborn\\logs") == 0 && c.logKeep == 10);

	Parse("address=10.0.0.2\nsbg_port=2000\nmatchmaking_port=2001\n", &c);
	CHECK(g_Warnings == 0 && !c.hostIsName && c.handover.backendInAddr == 0x0A000002u);
	CHECK(strcmp(c.handover.backendHost, "10.0.0.2") == 0);
	CHECK(c.handover.sbgPort == 2000 && c.handover.matchmakingPort == 2001);

	Parse("address=backend.lan\n", &c);
	CHECK(g_Warnings == 0 && c.hostIsName && strcmp(c.handover.backendHost, "backend.lan") == 0);

	/* longer than the Grid URL allows, including a padded IPv4 */
	Parse("address=a-very-long-host.lan\n", &c);
	CHECK(g_Warnings == 1 && strlen(c.handover.backendHost) <= REVIVAL_GRID_HOST_MAX);
	Parse("address=00000000000000000001.2.3.4\n", &c);
	CHECK(g_Warnings == 1 && strlen(c.handover.backendHost) <= REVIVAL_GRID_HOST_MAX);

	Parse("log_sink=both\nlog_level=debug\nlog_debug_also=none\nlog_buffer_kb=4096\n"
		  "log_flush_ms=250\nlog_tags=-netlog\nlog_dir=logs\\run\\\nlog_keep=3\n",
		&c);
	CHECK(g_Warnings == 0 && c.handover.logSink == (LOG_SINK_DEBUG | LOG_SINK_FILE));
	CHECK(c.handover.logLevelSet && c.handover.logLevel == LOG_LEVEL_DEBUG);
	CHECK(c.handover.logDebugAlso == LOG_LEVEL_NONE && c.handover.logBufferKb == 4096);
	CHECK(c.handover.logFlushMs == 250 && strcmp(c.handover.logTags, "-netlog") == 0);
	CHECK(strcmp(c.logDir, "logs\\run") == 0 && c.logKeep == 3);

	Parse("log_buffer_kb=3000\nlog_dir=..\\x\nlog_keep=0\nsbg_port=0\nbogus=1\n", &c);
	CHECK(g_Warnings == 5 && c.handover.logBufferKb == 2048 && c.logKeep == 10);
	CHECK(strcmp(c.logDir, "1vs100Reborn\\logs") == 0 && c.handover.sbgPort == 1000);
}
