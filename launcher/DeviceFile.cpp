#include "DeviceFile.h"

static NTSTATUS OpenDeviceFile(const CHAR *path, HANDLE *h)
{
	ANSI_STRING name;
	OBJECT_ATTRIBUTES attrs;
	IO_STATUS_BLOCK iosb;
	RtlInitAnsiString(&name, path);
	InitializeObjectAttributes(&attrs, &name, OBJ_CASE_INSENSITIVE, NULL);
	return NtOpenFile(h, GENERIC_READ | SYNCHRONIZE, &attrs, &iosb, FILE_SHARE_READ,
		0x20 /* FILE_SYNCHRONOUS_IO_NONALERT */);
}

BOOL DeviceFileExists(const CHAR *path)
{
	HANDLE h = NULL;
	if (OpenDeviceFile(path, &h) < 0)
		return FALSE;
	NtClose(h);
	return TRUE;
}

LONG ReadDeviceFile(const CHAR *path, CHAR *buf, DWORD cap, NTSTATUS *st)
{
	IO_STATUS_BLOCK iosb;
	HANDLE h = NULL;
	*st = OpenDeviceFile(path, &h);
	if (*st < 0)
		return -1;
	XMemSet(&iosb, 0, sizeof(iosb));
	*st = NtReadFile(h, NULL, NULL, NULL, &iosb, buf, cap - 1, NULL);
	NtClose(h);
	return *st < 0 ? 0 : (LONG)iosb.Information;
}

#define FILE_OPEN_IF_DISPOSITION 3
#define FILE_OVERWRITE_IF_DISPOSITION 5

static VOID InitPath(ANSI_STRING *name, OBJECT_ATTRIBUTES *attrs, const CHAR *path)
{
	RtlInitAnsiString(name, path);
	InitializeObjectAttributes(attrs, name, OBJ_CASE_INSENSITIVE, NULL);
}

NTSTATUS WriteDeviceFile(const CHAR *path, const VOID *data, DWORD len, DWORD offset, BOOL truncate)
{
	ANSI_STRING name;
	OBJECT_ATTRIBUTES attrs;
	IO_STATUS_BLOCK iosb;
	LARGE_INTEGER at;
	HANDLE h = NULL;
	InitPath(&name, &attrs, path);
	NTSTATUS st = NtCreateFile(&h, GENERIC_WRITE | SYNCHRONIZE, &attrs, &iosb, NULL,
		FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ,
		truncate ? FILE_OVERWRITE_IF_DISPOSITION : FILE_OPEN_IF_DISPOSITION,
		FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
	if (st < 0)
		return st;
	at.QuadPart = offset;
	st = len ? NtWriteFile(h, NULL, NULL, NULL, &iosb, (PVOID)data, len, &at) : 0;
	NtClose(h);
	return st;
}

NTSTATUS CreateDeviceDirectory(const CHAR *path)
{
	ANSI_STRING name;
	OBJECT_ATTRIBUTES attrs;
	IO_STATUS_BLOCK iosb;
	HANDLE h = NULL;
	InitPath(&name, &attrs, path);
	NTSTATUS st = NtCreateFile(&h, FILE_LIST_DIRECTORY | SYNCHRONIZE, &attrs, &iosb, NULL,
		FILE_ATTRIBUTE_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF_DISPOSITION,
		FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
	if (st >= 0)
		NtClose(h);
	return st;
}

NTSTATUS DeleteDeviceFile(const CHAR *path)
{
	ANSI_STRING name;
	OBJECT_ATTRIBUTES attrs;
	IO_STATUS_BLOCK iosb;
	FILE_DISPOSITION_INFO disposition;
	HANDLE h = NULL;
	InitPath(&name, &attrs, path);
	NTSTATUS st = NtOpenFile(&h, DELETE | SYNCHRONIZE, &attrs, &iosb, 0,
		FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
	if (st < 0)
		return st;
	disposition.DeleteFile = TRUE;
	st = NtSetInformationFile(
		h, &iosb, &disposition, sizeof(disposition), FileDispositionInformation);
	NtClose(h);
	return st;
}
