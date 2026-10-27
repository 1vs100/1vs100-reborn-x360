/* Title passes start when the target title image loads. */
#include "KernelLoadHook.h"
#include "TitleLifecycle.h"
#include "../registry/PatchHelpers.h"
#include "../config/PatchSwitches.h"
#include "../platform/ImportHook.h"
#include "../platform/Log.h"
#include "../platform/Text.h"
#include "../platform/SpinLock.h"
#include "../registry/PatchRegistry.h"

/* Installed once per plugin load. */
static SpinLock g_KernelHooked = SPINLOCK_INIT;

static BOOL NameLooksLikeTarget(LPCSTR name)
{
	DWORD i;
	char buf[128];
	if (!name)
		return FALSE;
	i = 0;
	while (i < sizeof(buf) - 1) {
		if (name[i] == 0)
			break;
		buf[i] = Text_LowerAscii(name[i]);
		i++;
	}
	buf[i] = 0;
	return Text_Contains(buf, "1vs100");
}

typedef NTSTATUS(NTAPI *XexLoadImage_t)(LPCSTR, DWORD, DWORD, PHANDLE);
typedef NTSTATUS(NTAPI *XexLoadImageFromMemory_t)(PVOID, DWORD, LPCSTR, DWORD, DWORD, PHANDLE);

static XexLoadImage_t orig_XexLoadImage = NULL;
static XexLoadImageFromMemory_t orig_XexLoadImageFromMemory = NULL;

static void MaybePatchLoaded(LPCSTR name)
{
	if (!NameLooksLikeTarget(name))
		return;
	LOG_INFO("core", "target image loaded: %s", name ? name : "(null)");
	ApplyTitlePatches();
}

/* A title never loads its own image here; its xam load runs the early pass on that thread. */
static void MaybeEarlyPatch(LPCSTR name)
{
	if (!name || !Text_Contains(name, "xam.xex"))
		return;
	ApplyEarlyTitlePatches();
}

static NTSTATUS NTAPI XexLoadImageHook(
	LPCSTR szXexName, DWORD dwModuleTypeFlags, DWORD dwMinimumVersion, PHANDLE pHandle)
{
	NTSTATUS st;
	if (!orig_XexLoadImage)
		return 0xC0000001;
	st = orig_XexLoadImage(szXexName, dwModuleTypeFlags, dwMinimumVersion, pHandle);
	LOG_INFO("core", "XexLoadImage %s -> %08X", szXexName ? szXexName : "(null)", st);
	if (NT_SUCCESS(st)) {
		MaybeEarlyPatch(szXexName);
		MaybePatchLoaded(szXexName);
	}
	return st;
}

/* xam launches titles through its own kernel import, bound at boot: the export hook never sees
   it. On return the title is mapped and no title thread has run; the early pass, and with it
   the entry wait, goes in here. */
typedef NTSTATUS(NTAPI *XexLoadExecutable_t)(PCHAR, PHANDLE, DWORD, DWORD);

static XexLoadExecutable_t orig_XexLoadExecutable = NULL;
static BOOL g_LoadExecutableHooked = FALSE;

static NTSTATUS NTAPI XexLoadExecutableHook(
	PCHAR szXexName, PHANDLE pHandle, DWORD dwModuleTypeFlags, DWORD dwMinimumVersion)
{
	NTSTATUS st;
	if (!orig_XexLoadExecutable)
		return 0xC0000001;
	st = orig_XexLoadExecutable(szXexName, pHandle, dwModuleTypeFlags, dwMinimumVersion);
	LOG_INFO("core", "XexLoadExecutable %s -> %08X", szXexName ? szXexName : "(null)", st);
	if (NT_SUCCESS(st))
		TryApplyEarlyTitlePatches();
	return st;
}

static NTSTATUS NTAPI XexLoadImageFromMemoryHook(PVOID pvXexBuffer, DWORD dwSize, LPCSTR szXexName,
	DWORD dwModuleTypeFlags, DWORD dwMinimumVersion, PHANDLE pHandle)
{
	NTSTATUS st;
	if (!orig_XexLoadImageFromMemory)
		return 0xC0000001;
	st = orig_XexLoadImageFromMemory(
		pvXexBuffer, dwSize, szXexName, dwModuleTypeFlags, dwMinimumVersion, pHandle);
	LOG_INFO("core", "XexLoadImageFromMemory %s -> %08X", szXexName ? szXexName : "(null)", st);
	if (NT_SUCCESS(st))
		MaybePatchLoaded(szXexName);
	return st;
}

VOID SetupKernelLoadHook()
{
	if (!SpinLock_TryLock(&g_KernelHooked))
		return;

	ReportPatchPhase("begin", "kernel");
	ApplyPatchPhase(PHASE_KERNEL);
	orig_XexLoadImage = (XexLoadImage_t)hookExportOrd(MODULE_KERNEL, 409, (DWORD)XexLoadImageHook);
	orig_XexLoadImageFromMemory = (XexLoadImageFromMemory_t)hookExportOrd(
		MODULE_KERNEL, 410, (DWORD)XexLoadImageFromMemoryHook);
	LOG_INFO("core", "kernel XexLoadImage orig=%p frommem orig=%p", orig_XexLoadImage,
		orig_XexLoadImageFromMemory);
	ReportPatch("XexLoadImage", orig_XexLoadImage ? "installed" : "failed", 409, "export");
	ReportPatch("XexLoadImageFromMemory", orig_XexLoadImageFromMemory ? "installed" : "failed", 410,
		"export");
	{
		PLDR_DATA_TABLE_ENTRY xam = (PLDR_DATA_TABLE_ENTRY)GetModuleHandle("xam.xex");
		ImportPatchResult r = IMPORT_UNRESOLVED;
		orig_XexLoadExecutable = (XexLoadExecutable_t)ResolveFunction(MODULE_KERNEL, 408);
		if (xam && orig_XexLoadExecutable)
			r = PatchModuleImport(xam, MODULE_KERNEL, 408, (DWORD)XexLoadExecutableHook);
		g_LoadExecutableHooked = r == IMPORT_PATCHED;
		ReportPatch("XexLoadExecutable (xam import)", g_LoadExecutableHooked ? "installed" : "failed",
			408, r == IMPORT_NO_MATCH ? "no_match" : (g_LoadExecutableHooked ? "import" : "unresolved"));
	}
	ReportPatchPhase("end", "kernel");
}

VOID RemoveKernelLoadHook()
{
	BOOL loadImage = !orig_XexLoadImage ||
		unhookExportOrd(MODULE_KERNEL, 409, (DWORD)XexLoadImageHook, (DWORD)orig_XexLoadImage);
	BOOL fromMemory = !orig_XexLoadImageFromMemory ||
		unhookExportOrd(MODULE_KERNEL, 410, (DWORD)XexLoadImageFromMemoryHook,
			(DWORD)orig_XexLoadImageFromMemory);
	/* xam outlives this DLL: its import must not point here after detach. */
	if (g_LoadExecutableHooked) {
		PLDR_DATA_TABLE_ENTRY xam = (PLDR_DATA_TABLE_ENTRY)GetModuleHandle("xam.xex");
		ImportPatchResult r = xam ? RestoreModuleImport(xam, MODULE_KERNEL, 408,
										(DWORD)XexLoadExecutableHook)
								  : IMPORT_UNRESOLVED;
		g_LoadExecutableHooked = r != IMPORT_PATCHED;
		if (g_LoadExecutableHooked)
			LOG_ERROR("core", "xam XexLoadExecutable import not restored (%d)", (int)r);
	}
	if (loadImage && fromMemory)
		LOG_INFO("core", "kernel XexLoadImage hooks removed");
	else
		LOG_ERROR("core",
			"kernel XexLoadImage hooks not removed (hooked on top since?): XexLoadImage=%u "
			"XexLoadImageFromMemory=%u",
			loadImage, fromMemory);
}
