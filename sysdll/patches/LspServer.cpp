/* XLSP server enumerator lists the backend: one item, then no more files. */
#include "PatchTables.h"
#include "../registry/PatchHelpers.h"
#include "../config/RevivalConfig.h"
#include "../platform/Log.h"

extern "C" void *_ReturnAddress(void);
#pragma intrinsic(_ReturnAddress)

/* The backend, as "MASSIVE". */
static XTITLE_SERVER_INFO activeServer;
static const char kServerDescription[XTITLE_SERVER_MAX_SERVER_INFO_LEN] = "MASSIVE";

static HANDLE g_LspEnumHandle = NULL;
static BOOL g_LspEnumServed = FALSE;

static VOID ResetLspEnumeration()
{
	g_LspEnumHandle = NULL;
	g_LspEnumServed = FALSE;
}

static int XamCreateEnumeratorHandleHook(DWORD user_index, HXAMAPP app_id, DWORD open_message,
	DWORD close_message, DWORD extra_size, DWORD item_count, DWORD flags, PHANDLE out_handle)
{
	int result = XamCreateEnumeratorHandle(
		user_index, app_id, open_message, close_message, extra_size, item_count, flags, out_handle);

	if (open_message == 0x58039 && result == ERROR_SUCCESS && out_handle) {
		g_LspEnumHandle = *out_handle;
		g_LspEnumServed = FALSE;
	}
	LOG_DEBUG("netlog",
		"XamCreateEnumeratorHandle app=%08X open=%08X close=%08X items=%u caller=%08X rc=%08X",
		(DWORD)app_id, open_message, close_message, item_count, (DWORD)_ReturnAddress(), result);
	return result;
}

static int XamEnumerateHook(HANDLE hEnum, DWORD dwFlags, PDWORD pvBuffer, DWORD cbBuffer,
	PDWORD pcItemsReturned, PXOVERLAPPED pOverlapped)
{
	if (hEnum != NULL && hEnum == g_LspEnumHandle) {
		DWORD errorCode;
		if (cbBuffer < sizeof(XTITLE_SERVER_INFO)) {
			LOG_DEBUG("netlog", "LSP enumerate: buffer too small (%u < %u)", cbBuffer,
				(DWORD)sizeof(XTITLE_SERVER_INFO));
			return ERROR_INSUFFICIENT_BUFFER;
		}
		memcpy(pvBuffer, &activeServer, sizeof(XTITLE_SERVER_INFO));
		errorCode = g_LspEnumServed ? ERROR_NO_MORE_FILES : ERROR_SUCCESS;
		LOG_DEBUG("netlog", "LSP enumerate: handle=%p item=%08X '%s' -> %u%s", hEnum,
			activeServer.inaServer.S_un.S_addr, activeServer.szServerInfo, errorCode,
			pOverlapped ? " (overlapped)" : "");
		g_LspEnumServed = TRUE;
		if (pcItemsReturned)
			*pcItemsReturned = 1;

		if (pOverlapped) {
			pOverlapped->InternalLow = errorCode;
			pOverlapped->InternalHigh = 1;
			pOverlapped->InternalContext = 0;
			pOverlapped->dwExtendedError = 0;
			if (pOverlapped->hEvent)
				SetEvent(pOverlapped->hEvent);
			return ERROR_IO_PENDING;
		}
		return errorCode;
	}

	return XamEnumerate(hEnum, dwFlags, pvBuffer, cbBuffer, pcItemsReturned, pOverlapped);
}

/* Forget on close: a reused handle value must not answer as the enumerator. */
#define KERNEL_ORD_NT_CLOSE 207u

static NTSTATUS NTAPI NtCloseHook(HANDLE handle)
{
	if (handle != NULL && handle == g_LspEnumHandle)
		ResetLspEnumeration();
	return NtClose(handle);
}

static VOID InstallLspEnumeratorClose(const PatchDescriptor *self)
{
	HookModuleImport(MODULE_KERNEL, self->param, (DWORD)self->target, self->id);
}

static VOID InstallLspEnumeratorCreate(const PatchDescriptor *self)
{
	activeServer.inaServer.S_un.S_addr = g_RevivalInAddr;
	memcpy(activeServer.szServerInfo, kServerDescription, XTITLE_SERVER_MAX_SERVER_INFO_LEN);
	InstallXamImport(self);
}

static const PatchDescriptor kRows[] = {
	{"XamCreateEnumeratorHandle", "lsp_enumerator_create", ON_TITLE,
		"Remembers the title's LSP enumerator handle", InstallLspEnumeratorCreate,
		ResetLspEnumeration, 590, (PVOID)XamCreateEnumeratorHandleHook},
	{"XamEnumerate", "lsp_enumerate", ON_TITLE,
		"The LSP enumerator lists the backend once, then no more files", InstallXamImport, NULL,
		592, (PVOID)XamEnumerateHook},
	{"NtClose (LSP enumerator close)", "lsp_enumerator_close", ON_TITLE,
		"Closing the LSP enumerator handle forgets it (kernel import)", InstallLspEnumeratorClose,
		NULL, KERNEL_ORD_NT_CLOSE, (PVOID)NtCloseHook},
};
const PatchTable kLspServerPatches = PATCH_TABLE(kRows);
