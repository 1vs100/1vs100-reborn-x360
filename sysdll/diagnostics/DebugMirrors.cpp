/* Mirror the title log sinks into the loader log. Investigation only: each costs a cave slot and formats text on title threads. */
#include "DiagnosticTables.h"
#include "../registry/PatchHelpers.h"
#include "../platform/Log.h"
#include "../platform/Text.h"

/* Mirror the text, then run the original. The trap returns once nop'd. */
typedef int (*XuluWideDebug_t)(const WCHAR *message);
static XuluWideDebug_t orig_XuluWideDebug = NULL;

static int XuluWideDebugHook(const WCHAR *message)
{
	CHAR line[512];
	DWORD outPos = Text_Append(line, 0, sizeof(line) - 1, "[XULU] ");
	if (message != NULL)
		outPos = Text_AppendWide(line, outPos, sizeof(line) - 1, message, 0xFFFFFFFFu);
	if (outPos == 0 || line[outPos - 1] != '\n')
		line[outPos++] = '\n';
	line[outPos] = 0;
	Log_EmitText(LOG_LEVEL_INFO, "xulu", line);

	return orig_XuluWideDebug(message);
}

typedef DWORD(__fastcall *LuaLogWrite_t)(
	DWORD logger, DWORD level, DWORD verbose, DWORD buf, const CHAR *text);
static LuaLogWrite_t g_LuaLogWriteOriginal = NULL;

static DWORD __fastcall LuaLogWriteHook(
	DWORD logger, DWORD level, DWORD verbose, DWORD buf, const CHAR *text)
{
	/* No rate cap: switch off with printsinkmirror. */
	LOG_DEBUG("lua", "%s", text ? text : "(null)");
	return g_LuaLogWriteOriginal ? g_LuaLogWriteOriginal(logger, level, verbose, buf, text) : 0;
}

static VOID InstallLuaLogWriteMirror(const PatchDescriptor *self)
{
	if (!HOOK_SITE(SITE_LUA_LOG_WRITE, LuaLogWriteHook, g_LuaLogWriteOriginal, self->id))
		return;
	LOG_INFO(
		"title", "Lua log write mirror installed at %08X", TitleSiteAddress(SITE_LUA_LOG_WRITE));
}

static VOID InstallXuluMirror(const PatchDescriptor *self)
{
	if (!HOOK_SITE(SITE_XULU_WIDE_DEBUG, XuluWideDebugHook, orig_XuluWideDebug, self->id))
		return;
	LOG_INFO("title", "XULU wide debug mirror installed at %08X",
		TitleSiteAddress(SITE_XULU_WIDE_DEBUG));
}

/* Built by hand: prefix, decimal, title-owned string. */
static VOID AppendDecimal(CHAR *buf, DWORD *pos, DWORD cap, DWORD value)
{
	QuizFmtUint(buf, pos, cap, value, 10, 0, 0, ' ');
}

/* Retail body is blr: genuinely a no-op. The hook replaces it outright. */

/* Per-frame block counter lines carry no signal and upset timing: dropped. */
static BOOL IsBlockCounterLine(const CHAR *fmt)
{
	return Text_StartsWith(fmt, "[%x]:Block++") || Text_StartsWith(fmt, "[%x]:--Block");
}

static VOID Nullsub2LoggerHook(
	PVOID channel, DWORD category, DWORD level, DWORD verbosity, const CHAR *fmt, ...)
{
	CHAR buf[512];
	DWORD pos = 0;
	va_list args;

	(void)channel;
	(void)verbosity;
	if (fmt == NULL || IsBlockCounterLine(fmt))
		return;
	pos = Text_Append(buf, pos, sizeof(buf), "[LOG cat=");
	AppendDecimal(buf, &pos, sizeof(buf), category);
	pos = Text_Append(buf, pos, sizeof(buf), " lvl=");
	AppendDecimal(buf, &pos, sizeof(buf), level);
	pos = Text_Append(buf, pos, sizeof(buf), "] ");
	va_start(args, fmt);
	QuizFormatInto(buf, &pos, sizeof(buf), fmt, args);
	va_end(args);
	buf[pos] = 0;
	Log_EmitText(LOG_LEVEL_INFO, "tlog", buf);
}

static VOID InstallNullsub2Mirror(const PatchDescriptor *self)
{
	DWORD unusedOriginal;

	if (!HOOK_SITE(SITE_NULLSUB2_LOGGER, Nullsub2LoggerHook, unusedOriginal, self->id))
		return;
	LOG_INFO(
		"title", "nullsub_2 log mirror installed at %08X", TitleSiteAddress(SITE_NULLSUB2_LOGGER));
}

/* Same family, one argument shorter: no verbosity. nullsub_1 logs lua_status at Block zero; nullsub_5 covers QNet tuning and thread disposal. */

static VOID Nullsub1Or5LoggerCore(
	const CHAR *tag, DWORD category, DWORD level, const CHAR *fmt, va_list args)
{
	CHAR buf[512];
	DWORD pos = 0;

	pos = Text_Append(buf, pos, sizeof(buf), "[");
	pos = Text_Append(buf, pos, sizeof(buf), tag);
	pos = Text_Append(buf, pos, sizeof(buf), " cat=");
	AppendDecimal(buf, &pos, sizeof(buf), category);
	pos = Text_Append(buf, pos, sizeof(buf), " lvl=");
	AppendDecimal(buf, &pos, sizeof(buf), level);
	pos = Text_Append(buf, pos, sizeof(buf), "] ");
	QuizFormatInto(buf, &pos, sizeof(buf), fmt, args);
	buf[pos] = 0;
	Log_EmitText(LOG_LEVEL_INFO, "tlog", buf);
}

static VOID Nullsub1LoggerHook(PVOID channel, DWORD category, DWORD level, const CHAR *fmt, ...)
{
	va_list args;

	(void)channel;
	va_start(args, fmt);
	Nullsub1Or5LoggerCore("LOG1", category, level, fmt, args);
	va_end(args);
}

static VOID Nullsub5LoggerHook(PVOID channel, DWORD category, DWORD level, const CHAR *fmt, ...)
{
	va_list args;

	(void)channel;
	va_start(args, fmt);
	Nullsub1Or5LoggerCore("LOG5", category, level, fmt, args);
	va_end(args);
}

static VOID InstallNullsub1Mirror(const PatchDescriptor *self)
{
	DWORD unusedOriginal;

	if (!HOOK_SITE(SITE_NULLSUB1_LOGGER, Nullsub1LoggerHook, unusedOriginal, self->id))
		return;
	LOG_INFO(
		"title", "nullsub_1 log mirror installed at %08X", TitleSiteAddress(SITE_NULLSUB1_LOGGER));
}

static VOID InstallNullsub5Mirror(const PatchDescriptor *self)
{
	DWORD unusedOriginal;

	if (!HOOK_SITE(SITE_NULLSUB5_LOGGER, Nullsub5LoggerHook, unusedOriginal, self->id))
		return;
	LOG_INFO(
		"title", "nullsub_5 log mirror installed at %08X", TitleSiteAddress(SITE_NULLSUB5_LOGGER));
}

/* Retail print builds the line then drops it. Read args with the pure lua_tolstring; non-strings log as <non-string>. */
typedef DWORD(__fastcall *LuaArgCount_t)(DWORD luaState);
typedef const CHAR *(__fastcall *LuaToLString_t)(DWORD luaState, int index, DWORD lenOut);
typedef int(__fastcall *LuaPrintSink_t)(int luaState);
static LuaPrintSink_t g_LuaPrintSinkOriginal = NULL;

static int __fastcall LuaPrintSinkHook(int luaState)
{
	LuaArgCount_t argCountFn = (LuaArgCount_t)TitleSiteAddress(SITE_LUA_ARG_COUNT);
	LuaToLString_t toLStringFn = (LuaToLString_t)TitleSiteAddress(SITE_LUA_TO_LSTRING);

	if (argCountFn && toLStringFn) {
		CHAR buf[512];
		DWORD pos = 0;
		DWORD argc = argCountFn((DWORD)luaState);
		DWORD i = 1;

		pos = Text_Append(buf, pos, sizeof(buf), "[PRINT] ");
		while (i <= argc) {
			const CHAR *s = toLStringFn((DWORD)luaState, (int)i, 0);
			if (i > 1)
				pos = Text_Append(buf, pos, sizeof(buf), "\t");
			pos = Text_Append(buf, pos, sizeof(buf), s ? s : "<non-string>");
			i++;
		}
		buf[pos] = 0;
		Log_EmitText(LOG_LEVEL_INFO, "lua", buf);
	}

	return g_LuaPrintSinkOriginal ? g_LuaPrintSinkOriginal(luaState) : 0;
}

static VOID InstallLuaPrintSinkMirror(const PatchDescriptor *self)
{
	static const SiteId kCalls[] = {SITE_LUA_PRINT_SINK, SITE_LUA_ARG_COUNT, SITE_LUA_TO_LSTRING};
	if (!REQUIRE_SITES(kCalls, self->id) ||
		!HOOK_SITE(SITE_LUA_PRINT_SINK, LuaPrintSinkHook, g_LuaPrintSinkOriginal, self->id))
		return;
	LOG_INFO(
		"title", "Lua print sink mirror installed at %08X", TitleSiteAddress(SITE_LUA_PRINT_SINK));
}

/* Shared switch: both mirror Lua output. */
static const PatchDescriptor kRows[] = {
	{"XuluWideDebug", "xulumirror", ON_TITLE, "XULU wide logger text mirrored to the log",
		InstallXuluMirror},
	{"LuaLogWrite", "printsinkmirror", ON_TITLE, "Lua log writes mirrored to the log",
		InstallLuaLogWriteMirror},
	{"Nullsub2Logger", "nullsub2mirror", ON_TITLE,
		"Retail nullsub_2 sink to the log",
		InstallNullsub2Mirror},
	{"Nullsub1Logger", "nullsub1mirror", ON_TITLE,
		"Retail nullsub_1 sink to the log", InstallNullsub1Mirror},
	{"Nullsub5Logger", "nullsub5mirror", ON_TITLE,
		"Retail nullsub_5 sink to the log", InstallNullsub5Mirror},
	{"LuaPrintSink", "printsinkmirror", ON_TITLE,
		"Lua print arguments to the log",
		InstallLuaPrintSinkMirror},
};
const PatchTable kDebugMirrorPatches = PATCH_TABLE(kRows);
