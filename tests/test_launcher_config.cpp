#include "check.h"
#include <string.h>
#include "../launcher/LauncherConfig.h"

static int g_Warnings;

static void Warn(const CHAR *fmt, ...)
{
	(void)fmt;
	g_Warnings++;
}

static BOOL Parse(const char *text, LauncherConfig *config)
{
	g_Warnings = 0;
	return LauncherConfig_Parse(text, (DWORD)strlen(text), config, Warn);
}

void TestLauncherConfig()
{
	LauncherConfig c;

	CHECK(!Parse("", &c) && g_Warnings == 1 && c.handover.backendHost[0] == 0);
	CHECK(c.handover.size == sizeof(c.handover));
	CHECK(c.handover.sbgPort == 1000 && c.handover.matchmakingPort == 1001);
	CHECK(c.handover.logSink == LOG_SINK_FILE && !c.handover.logLevelSet);
	CHECK(c.handover.logDebugAlso == LOG_LEVEL_WARN && c.handover.logBufferKb == 2048);
	CHECK(strcmp(c.logDir, "1vs100Reborn\\logs") == 0 && c.logKeep == 10);

	CHECK(Parse("[backend]\nhost=10.0.0.2\nsbg_port=2000\nmatchmaking_port=2001\n", &c));
	CHECK(g_Warnings == 0 && !c.hostIsName && c.handover.backendInAddr == 0x0A000002u);
	CHECK(strcmp(c.handover.backendHost, "10.0.0.2") == 0);
	CHECK(c.handover.sbgPort == 2000 && c.handover.matchmakingPort == 2001);

	CHECK(Parse("[Backend]\nHost=backend.lan\n", &c));
	CHECK(g_Warnings == 0 && c.hostIsName && strcmp(c.handover.backendHost, "backend.lan") == 0);

	/* keys belong to their section */
	CHECK(!Parse("host=10.0.0.2\n[log]\nhost=10.0.0.2\n", &c) && g_Warnings == 3);

	/* longer than the Grid URL allows, including a padded IPv4 */
	CHECK(!Parse("[backend]\nhost=a-very-long-host.lan\n", &c) && c.handover.backendHost[0] == 0);
	CHECK(!Parse("[backend]\nhost=00000000000000000001.2.3.4\n", &c));
	CHECK(c.handover.backendHost[0] == 0);
	CHECK(!Parse("[backend]\nhost=bad_name\n", &c) && g_Warnings == 2);

	CHECK(Parse("[backend]\nhost=h\n[log]\nsink=both\nlevel=debug\ndebug_also=none\n"
				"buffer_kb=4096\nflush_ms=250\ntags=-netlog\ndir=logs\\run\\\nkeep=3\n",
		&c));
	CHECK(g_Warnings == 0 && c.handover.logSink == (LOG_SINK_DEBUG | LOG_SINK_FILE));
	CHECK(c.handover.logLevelSet && c.handover.logLevel == LOG_LEVEL_DEBUG);
	CHECK(c.handover.logDebugAlso == LOG_LEVEL_NONE && c.handover.logBufferKb == 4096);
	CHECK(c.handover.logFlushMs == 250 && strcmp(c.handover.logTags, "-netlog") == 0);
	CHECK(strcmp(c.logDir, "logs\\run") == 0 && c.logKeep == 3);

	/* each bad line is reported; a valid host does not make the config valid */
	CHECK(!Parse("[backend]\nhost=h\nsbg_port=0\n[log]\nbuffer_kb=3000\ndir=..\\x\nkeep=0\n"
				 "level=verbose\nbogus=1\n[other]\nx=1\n",
		&c));
	CHECK(g_Warnings == 7 && !c.handover.logLevelSet);
}
