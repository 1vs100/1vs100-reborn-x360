#pragma once
#include <xtl.h>
#include <xkelib.h>

/* Files by kernel object path. */

#define HDD_ROOT "\\Device\\Harddisk0\\Partition1"

/* Reads up to cap-1 bytes; -1 when it cannot be opened. */
LONG ReadDeviceFile(const CHAR *path, CHAR *buf, DWORD cap, NTSTATUS *st);
BOOL DeviceFileExists(const CHAR *path);
/* Creates when missing; truncate empties first. */
NTSTATUS WriteDeviceFile(const CHAR *path, const VOID *data, DWORD len, DWORD offset, BOOL truncate);
/* One level: parent must exist. */
NTSTATUS CreateDeviceDirectory(const CHAR *path);
NTSTATUS DeleteDeviceFile(const CHAR *path);
