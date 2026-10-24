#include "ContentDirectoryScan.h"
#include "../platform/Log.h"
#include "../platform/Text.h"

static BOOL IsHexChar(CHAR c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

/* Keeps the name when it is a 40 or 42 digit hex package id. */
static DWORD AddContentName(
	const CHAR *name, DWORD nameLen, ContentFallbackItem *items, DWORD count, DWORD maxItems)
{
	if (count >= maxItems || (nameLen != 40 && nameLen != 42))
		return count;
	for (DWORD i = 0; i < nameLen; ++i)
		if (!IsHexChar(name[i]))
			return count;
	XMemSet(items[count].fileName, 0, sizeof(items[count].fileName));
	memcpy(items[count].fileName, name, nameLen);
	return count + 1;
}

/* Win32 listing; dir has no trailing backslash. */
static BOOL ScanWin32(const CHAR *dir, ContentFallbackItem *items, DWORD maxItems, DWORD *pCount)
{
	CHAR pattern[160];
	DWORD n = Text_Append(pattern, 0, sizeof(pattern), dir);
	Text_Append(pattern, n, sizeof(pattern), "\\*");
	WIN32_FIND_DATAA fd;
	HANDLE hFind = FindFirstFileA(pattern, &fd);
	if (hFind == INVALID_HANDLE_VALUE) {
		LOG_INFO("content", "Content enumeration fallback: Win32 list of %s failed (err=%08X)",
			pattern, GetLastError());
		return FALSE;
	}
	DWORD count = 0;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		DWORD len = Text_Length(fd.cFileName, 64);
		count = AddContentName(fd.cFileName, len, items, count, maxItems);
	} while (FindNextFileA(hFind, &fd));
	FindClose(hFind);
	*pCount = count;
	return TRUE;
}

/* Native listing: no DOS-name translation, raw \Device path. */
typedef struct _CONTENT_DIR_INFO {
	ULONG NextEntryOffset;
	ULONG FileIndex;
	LARGE_INTEGER Times[4];
	LARGE_INTEGER EndOfFile;
	LARGE_INTEGER AllocationSize;
	ULONG FileAttributes;
	ULONG FileNameLength;
	CHAR FileName[1];
} CONTENT_DIR_INFO;

static BOOL ScanNative(const CHAR *dir, ContentFallbackItem *items, DWORD maxItems, DWORD *pCount)
{
	ANSI_STRING name;
	OBJECT_ATTRIBUTES attrs;
	IO_STATUS_BLOCK iosb;
	HANDLE hDir = NULL;
	RtlInitAnsiString(&name, dir);
	InitializeObjectAttributes(&attrs, &name, OBJ_CASE_INSENSITIVE, NULL);
	NTSTATUS status = NtOpenFile(&hDir, 0x00100001 /* SYNCHRONIZE|FILE_LIST_DIRECTORY */, &attrs, &iosb, FILE_SHARE_READ | FILE_SHARE_WRITE, 0x21 /* DIRECTORY_FILE|SYNCHRONOUS_IO_NONALERT */);
	if (status < 0) {
		LOG_INFO(
			"content", "Content enumeration fallback: NtOpenFile(%s) failed (%08X)", dir, status);
		return FALSE;
	}
	/* Static: shared buffer, only touched under the lock. */
	static BYTE buffer[4096];
	static SpinLock g_ScanBufferLock = SPINLOCK_INIT;
	DWORD count = 0;
	DWORD restart = 1;
	SpinLock_LockSleeping(&g_ScanBufferLock);
	for (;;) {
		XMemSet(&iosb, 0, sizeof(iosb));
		status = NtQueryDirectoryFile(
			hDir, NULL, NULL, NULL, &iosb, buffer, sizeof(buffer), NULL, restart);
		restart = 0;
		if (status < 0)
			break; /* STATUS_NO_MORE_FILES ends the listing */
		BYTE *entry = buffer;
		for (;;) {
			CONTENT_DIR_INFO *info = (CONTENT_DIR_INFO *)entry;
			if (!(info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				count =
					AddContentName(info->FileName, info->FileNameLength, items, count, maxItems);
			if (info->NextEntryOffset == 0)
				break;
			entry += info->NextEntryOffset;
		}
	}
	SpinLock_Unlock(&g_ScanBufferLock);
	NtClose(hDir);
	*pCount = count;
	return TRUE;
}

DWORD ScanContentDirectory(DWORD titleId, ContentFallbackItem *items, DWORD maxItems)
{
	/* "\Content\0000000000000000\" + 8 hex + "\00000002" + NUL is 44 bytes. */
	CHAR tail[48];
	DWORD t = Text_Append(tail, 0, sizeof(tail), "\\Content\\0000000000000000\\");
	t = Text_AppendHex(tail, t, sizeof(tail), titleId, 8);
	Text_Append(tail, t, sizeof(tail), "\\00000002");

	static const CHAR *kRoots[] = {"hdd:", "\\Device\\Harddisk0\\Partition1"};
	DWORD count = 0;
	for (DWORD r = 0; r < 2; ++r) {
		CHAR dir[128];
		DWORD d = Text_Append(dir, 0, sizeof(dir), kRoots[r]);
		Text_Append(dir, d, sizeof(dir), tail);
		BOOL ok = (r == 0) ? ScanWin32(dir, items, maxItems, &count)
						   : ScanNative(dir, items, maxItems, &count);
		if (ok) {
			LOG_INFO(
				"content", "Content enumeration fallback: listed %s -> %u package(s)", dir, count);
			return count;
		}
	}
	return 0;
}

/* One cursor per live enumerator. Serve the listing once per enumerator, then answer with the ORIGINAL result, so a missing package fails instead of spinning. */
#define CONTENT_ENUM_STALE_MS 2000
#define CONTENT_ENUM_CURSORS 8
static ContentEnumCursor g_ContentEnumCursors[CONTENT_ENUM_CURSORS];
static DWORD g_ContentEnumEvict = 0;
SpinLock g_ContentEnumLock = SPINLOCK_INIT;

ContentEnumCursor *GetContentCursor(DWORD key)
{
	const DWORD now = GetTickCount();
	for (DWORD i = 0; i < CONTENT_ENUM_CURSORS; ++i) {
		if (g_ContentEnumCursors[i].key == key) {
			if (now - g_ContentEnumCursors[i].tick > CONTENT_ENUM_STALE_MS)
				g_ContentEnumCursors[i].next = 0; /* new enumerator at a reused address */
			g_ContentEnumCursors[i].tick = now;
			return &g_ContentEnumCursors[i];
		}
	}
	for (DWORD i = 0; i < CONTENT_ENUM_CURSORS; ++i) {
		if (g_ContentEnumCursors[i].key == 0) {
			g_ContentEnumCursors[i].key = key;
			g_ContentEnumCursors[i].next = 0;
			g_ContentEnumCursors[i].tick = now;
			return &g_ContentEnumCursors[i];
		}
	}
	ContentEnumCursor *slot = &g_ContentEnumCursors[g_ContentEnumEvict++ % CONTENT_ENUM_CURSORS];
	slot->key = key;
	slot->next = 0;
	slot->tick = now;
	return slot;
}

VOID ResetContentCursors()
{
	SpinLock_LockSleeping(&g_ContentEnumLock);
	XMemSet(g_ContentEnumCursors, 0, sizeof(g_ContentEnumCursors));
	g_ContentEnumEvict = 0;
	SpinLock_Unlock(&g_ContentEnumLock);
}

BOOL ResolveCurrentTitleId(DWORD *pTitleId)
{
	/* TitleID is a DWORD at +0xC; read it directly. */
	PXEX_EXECUTION_ID pExecId = NULL;
	if (XamGetExecutionId(&pExecId) < 0 || pExecId == NULL)
		return FALSE;
	*pTitleId = *(DWORD *)((BYTE *)pExecId + 0xC);
	return TRUE;
}

BOOL ResolveHddDeviceId(DWORD *pDeviceId)
{
	DWORD cbBuffer = 0;
	HANDLE hEnum = NULL;
	DWORD createResult =
		XamContentCreateDeviceEnumerator(XCONTENTDEVICETYPE_HDD, 0, 1, &cbBuffer, &hEnum);
	if (createResult != ERROR_SUCCESS || hEnum == NULL) {
		LOG_INFO("content", "Launch content fallback: device enumerator create failed (%08X)",
			createResult);
		return FALSE;
	}

	BYTE deviceRecord[80];
	XMemSet(deviceRecord, 0, sizeof(deviceRecord));
	DWORD itemsReturned = 0;
	XamEnumerate(hEnum, 0, deviceRecord, sizeof(deviceRecord), &itemsReturned, NULL);
	CloseHandle(hEnum);

	if (itemsReturned == 0) {
		LOG_INFO("content", "Launch content fallback: no HDD device returned by enumerator");
		return FALSE;
	}
	*pDeviceId = *(DWORD *)deviceRecord;
	return TRUE;
}
