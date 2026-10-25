/* Title threads only copy under a spin lock; a writer thread empties the ring to disk. */
/* The file opens, appends and closes on every flush, so a crash loses only unflushed lines. */
#include "Log.h"
#include "Format.h"
#include "LogConfig.h"
#include "SpinLock.h"
#include "Text.h"

#ifndef FILE_OPEN_IF
#define FILE_OPEN_IF 3
#endif

#if defined(LOADER_DIAGNOSTICS)
static const DWORD kDefaultLevel = LOG_LEVEL_DEBUG;
#else
static const DWORD kDefaultLevel = LOG_LEVEL_INFO;
#endif
static const DWORD kMinBufferBytes = LOG_BUFFER_MIN_KB * 1024;
static const DWORD kWriterPollMs = 50;
static const DWORD kRecordHeader = 12; /* tick, thread id, length */
static const DWORD kPoolTag = 0x51534C47;

/* Until configured: debug channel, kept for the file. */
static BYTE g_EarlyRing[32 * 1024];

static SpinLock g_RingLock = SPINLOCK_INIT;
static BYTE *g_Ring = g_EarlyRing;
static DWORD g_RingSize = sizeof(g_EarlyRing); /* a power of two */
static DWORD g_Head = 0;                       /* bytes ever appended */
static DWORD g_Tail = 0;                       /* bytes ever consumed */
static BYTE *g_PoolRing = NULL;

static volatile LONG g_Configured = FALSE;
static volatile DWORD g_Sink = LOG_SINK_DEBUG | LOG_SINK_FILE;
static DWORD g_Level = LOG_LEVEL_DEBUG;
static DWORD g_DebugAlso = LOG_LEVEL_WARN;
static DWORD g_FlushMs = LOG_DEFAULT_FLUSH_MS;
static CHAR g_Tags[LOG_TAGS_BYTES];
static CHAR g_File[LOG_PATH_BYTES];
static DWORD g_FileOffset = 0;

static volatile LONG g_Dropped = 0;
static volatile LONG g_Wake = FALSE;
static volatile LONG g_Stop = FALSE;
static volatile LONG g_WriterDone = FALSE;
static volatile LONG g_WriterRunning = FALSE;

BOOL Log_Wanted(LogLevel level, const CHAR *tag)
{
	if ((DWORD)level > g_Level)
		return FALSE;
	return g_Tags[0] == 0 || LogConfig_TagAllowed(g_Tags, tag);
}

static VOID RingCopyIn(DWORD at, const BYTE *src, DWORD n)
{
	DWORD i = 0, mask = g_RingSize - 1;
	while (i < n) {
		g_Ring[(at + i) & mask] = src[i];
		i++;
	}
}

static VOID RingCopyOut(DWORD at, BYTE *dst, DWORD n)
{
	DWORD i = 0, mask = g_RingSize - 1;
	while (i < n) {
		dst[i] = g_Ring[(at + i) & mask];
		i++;
	}
}

/* FALSE when full; the line counts as dropped. */
static BOOL RingPush(const CHAR *text, DWORD len)
{
	DWORD header[3], record, used;
	BOOL pushed = FALSE;
	if (len > LOG_LINE_BYTES)
		len = LOG_LINE_BYTES;
	header[0] = GetTickCount();
	header[1] = GetCurrentThreadId();
	header[2] = len;
	record = kRecordHeader + ((len + 3) & ~3u);
	SpinLock_Lock(&g_RingLock);
	used = g_Head - g_Tail;
	if (used + record <= g_RingSize) {
		RingCopyIn(g_Head, (const BYTE *)header, kRecordHeader);
		RingCopyIn(g_Head + kRecordHeader, (const BYTE *)text, len);
		g_Head += record;
		used += record;
		pushed = TRUE;
	}
	SpinLock_Unlock(&g_RingLock);
	if (!pushed)
		InterlockedIncrement(&g_Dropped);
	else if (used > g_RingSize / 2)
		InterlockedExchange(&g_Wake, TRUE);
	return pushed;
}

static VOID Log_Route(LogLevel level, const CHAR *text)
{
	DWORD sink = g_Sink;
	BOOL configured = g_Configured;
	BOOL toDebug = (sink & LOG_SINK_DEBUG) != 0;
	if (!toDebug && configured && g_DebugAlso != LOG_LEVEL_NONE && (DWORD)level <= g_DebugAlso)
		toDebug = TRUE;
	if (toDebug)
		OutputDebugStringA(text);
	if (sink & LOG_SINK_FILE) {
		RingPush(text, Text_Length(text, LOG_LINE_BYTES));
		if (level == LOG_LEVEL_ERROR)
			InterlockedExchange(&g_Wake, TRUE);
	}
}

VOID Log_EmitText(LogLevel level, const CHAR *tag, const CHAR *text)
{
	if (text == NULL || !Log_Wanted(level, tag))
		return;
	Log_Route(level, text);
}

VOID Log_RouteBuilt(LogLevel level, const CHAR *text)
{
	Log_Route(level, text);
}

/* System pool: the default pool belongs to the configuring thread's process. Halves on failure. */
static BYTE *AllocateRing(DWORD bytes, DWORD *size)
{
	DWORD n = bytes;
	while (n >= kMinBufferBytes) {
		BYTE *p = (BYTE *)ExAllocatePoolTypeWithTag(n, kPoolTag, PoolTypeSystem);
		if (p != NULL) {
			*size = n;
			return p;
		}
		n /= 2;
	}
	return NULL;
}

VOID Log_Configure(const LogSettings *settings)
{
	DWORD size = 0;
	BYTE *ring = NULL;
	BOOL wantFile;
	if (settings == NULL || g_Configured)
		return;
	wantFile = (settings->sink & LOG_SINK_FILE) != 0 && settings->file != NULL &&
		settings->file[0] != 0;
	if (wantFile)
		ring = AllocateRing(settings->bufferKb * 1024, &size);

	g_Level = settings->levelSet ? settings->level : kDefaultLevel;
	g_DebugAlso = settings->debugAlso;
	g_FlushMs = settings->flushMs;
	g_Tags[0] = 0;
	if (settings->tags)
		Text_Append(g_Tags, 0, sizeof(g_Tags), settings->tags);
	g_File[0] = 0;
	if (wantFile)
		Text_Append(g_File, 0, sizeof(g_File), settings->file);
	g_FileOffset = settings->fileBytes;

	SpinLock_Lock(&g_RingLock);
	if (ring != NULL) {
		/* Early lines move over, oldest first. */
		DWORD used = g_Head - g_Tail, i = 0;
		BYTE *oldRing = g_Ring;
		DWORD oldMask = g_RingSize - 1, oldTail = g_Tail;
		if (used > size)
			used = 0;
		while (i < used) {
			ring[i] = oldRing[(oldTail + i) & oldMask];
			i++;
		}
		g_Ring = ring;
		g_RingSize = size;
		g_PoolRing = ring;
		g_Head = used;
		g_Tail = 0;
		g_Sink = settings->sink;
	} else {
		/* Without a file the early lines stay debug-channel only. */
		g_Head = g_Tail = 0;
		g_Sink = settings->sink & LOG_SINK_DEBUG;
		if (wantFile)
			g_Sink |= LOG_SINK_DEBUG;
	}
	SpinLock_Unlock(&g_RingLock);
	InterlockedExchange(&g_Configured, TRUE);

	if (wantFile && ring == NULL)
		LOG_ERROR("core", "log: no memory for the file buffer, logging to the debug channel");
	LOG_INFO("core", "log: sink=%s%s level=%u debug_also=%d buffer=%u flush=%ums file=%s tags=%s",
		(g_Sink & LOG_SINK_DEBUG) ? "debug" : "", (g_Sink & LOG_SINK_FILE) ? "+file" : "",
		g_Level, (int)g_DebugAlso, g_PoolRing ? g_RingSize : 0, g_FlushMs,
		g_File[0] ? g_File : "-", g_Tags[0] ? g_Tags : "-");
}


static CHAR g_Out[64 * 1024];
static BYTE g_Rec[LOG_LINE_BYTES];

static NTSTATUS AppendToFile(const CHAR *data, DWORD len)
{
	ANSI_STRING name;
	OBJECT_ATTRIBUTES attrs;
	IO_STATUS_BLOCK iosb;
	LARGE_INTEGER offset;
	HANDLE h = NULL;
	NTSTATUS st;
	RtlInitAnsiString(&name, g_File);
	InitializeObjectAttributes(&attrs, &name, OBJ_CASE_INSENSITIVE, NULL);
	st = NtCreateFile(&h, GENERIC_WRITE | SYNCHRONIZE, &attrs, &iosb, NULL, FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ, FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
	if (st < 0)
		return st;
	offset.QuadPart = g_FileOffset;
	st = NtWriteFile(h, NULL, NULL, NULL, &iosb, (PVOID)data, len, &offset);
	if (st >= 0)
		g_FileOffset += (DWORD)iosb.Information;
	NtClose(h);
	return st;
}

/* File failed: debug channel from here on. */
static VOID FallBackToDebug(NTSTATUS st)
{
	SpinLock_Lock(&g_RingLock);
	g_Sink = (g_Sink & ~LOG_SINK_FILE) | LOG_SINK_DEBUG;
	g_Head = g_Tail = 0;
	SpinLock_Unlock(&g_RingLock);
	LOG_ERROR("core", "log: writing %s failed (%08X), logging to the debug channel", g_File,
		(DWORD)st);
}

static BOOL Drain()
{
	DWORD head, at, outPos = 0;
	LONG dropped;
	SpinLock_Lock(&g_RingLock);
	head = g_Head;
	SpinLock_Unlock(&g_RingLock);
	at = g_Tail;
	dropped = InterlockedExchange(&g_Dropped, 0);
	if (dropped > 0) {
		CHAR line[96];
		DWORD n = Text_Append(line, 0, sizeof(line), "[QS][W][core] log: ");
		QuizFmtUint(line, &n, sizeof(line), (DWORD)dropped, 10, 0, 0, ' ');
		n = Text_Append(line, n, sizeof(line), " line(s) dropped, the buffer was full\n");
		LogConfig_FormatCaptureLine(
			g_Out, &outPos, sizeof(g_Out), GetTickCount(), GetCurrentThreadId(), line, n);
	}
	while (at != head) {
		DWORD header[3], len, record;
		RingCopyOut(at, (BYTE *)header, kRecordHeader);
		len = header[2];
		record = kRecordHeader + ((len + 3) & ~3u);
		/* Flush when full; a record always fits an empty buffer. */
		if (outPos + len + 64 > sizeof(g_Out)) {
			NTSTATUS st = AppendToFile(g_Out, outPos);
			if (st < 0) {
				FallBackToDebug(st);
				return FALSE;
			}
			outPos = 0;
		}
		RingCopyOut(at + kRecordHeader, g_Rec, len);
		LogConfig_FormatCaptureLine(
			g_Out, &outPos, sizeof(g_Out), header[0], header[1], (const CHAR *)g_Rec, len);
		at += record;
	}
	if (outPos > 0) {
		NTSTATUS st = AppendToFile(g_Out, outPos);
		if (st < 0) {
			FallBackToDebug(st);
			return FALSE;
		}
	}
	SpinLock_Lock(&g_RingLock);
	g_Tail = at;
	SpinLock_Unlock(&g_RingLock);
	return TRUE;
}

DWORD Log_WriterMain(PVOID unused)
{
	DWORD lastFlush = GetTickCount();
	(void)unused;
	InterlockedExchange(&g_WriterRunning, TRUE);
	while (!g_Stop) {
		Sleep(kWriterPollMs);
		if (!g_Configured || !(g_Sink & LOG_SINK_FILE))
			continue;
		if (InterlockedExchange(&g_Wake, FALSE) || GetTickCount() - lastFlush >= g_FlushMs) {
			Drain();
			lastFlush = GetTickCount();
		}
	}
	if (g_Configured && (g_Sink & LOG_SINK_FILE))
		Drain();
	InterlockedExchange(&g_WriterDone, TRUE);
	return 0;
}

VOID Log_RequestFlush()
{
	InterlockedExchange(&g_Wake, TRUE);
}

VOID Log_Shutdown(DWORD waitMs)
{
	DWORD start = GetTickCount();
	BYTE *pool;
	InterlockedExchange(&g_Stop, TRUE);
	if (g_WriterRunning) {
		while (!g_WriterDone) {
			if (GetTickCount() - start > waitMs)
				break;
			Sleep(10);
		}
	}
	SpinLock_Lock(&g_RingLock);
	g_Sink = LOG_SINK_DEBUG;
	pool = g_WriterDone ? g_PoolRing : NULL;
	if (pool != NULL) {
		g_Ring = g_EarlyRing;
		g_RingSize = sizeof(g_EarlyRing);
		g_Head = g_Tail = 0;
		g_PoolRing = NULL;
	}
	SpinLock_Unlock(&g_RingLock);
	if (pool != NULL)
		ExFreePool(pool);
}
