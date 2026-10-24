/* Diagnostics: log-only hooks on the 1149 title's avatar init chain. */
#include "DiagnosticTables.h"
#include "../registry/PatchHelpers.h"
#include "../platform/Log.h"

/* Avatar-ready flag goes -1 on failure; every Lua Avatar() then returns nil. */
typedef int(__fastcall *AvatarStep_t)(int a1, int a2, int a3, int a4, int a5);
static AvatarStep_t g_AvatarSystemInitOriginal = NULL;
static AvatarStep_t g_AvatarInitPreStepOriginal = NULL;
static AvatarStep_t g_AvatarInitDeviceOriginal = NULL;
static AvatarStep_t g_AvatarInitAnimsOriginal = NULL;

static int __fastcall AvatarSystemInitHook(int a1, int a2, int a3, int a4, int a5)
{
	int hr = g_AvatarSystemInitOriginal(a1, a2, a3, a4, a5);
	LOG_INFO(
		"xam", "avatar init(%d, flags=%d, %d, %d, device=%08X) -> %08X", a1, a2, a3, a4, a5, hr);
	return hr;
}

static int __fastcall AvatarInitPreStepHook(int a1, int a2, int a3, int a4, int a5)
{
	int hr = g_AvatarInitPreStepOriginal(a1, a2, a3, a4, a5);
	LOG_INFO("xam", "avatar init pre-step -> %08X", hr);
	return hr;
}

static int __fastcall AvatarInitDeviceHook(int a1, int a2, int a3, int a4, int a5)
{
	int hr = g_AvatarInitDeviceOriginal(a1, a2, a3, a4, a5);
	LOG_INFO("xam", "avatar init device(%08X) -> %08X", a1, hr);
	return hr;
}

static int __fastcall AvatarInitAnimsHook(int a1, int a2, int a3, int a4, int a5)
{
	int hr = g_AvatarInitAnimsOriginal(a1, a2, a3, a4, a5);
	LOG_INFO("xam", "avatar init anims(%08X) -> %08X", a1, hr);
	return hr;
}

static VOID InstallAvatarInitLog(const PatchDescriptor *self)
{
	HOOK_SITE(SITE_AVATAR_SYSTEM_INIT, AvatarSystemInitHook, g_AvatarSystemInitOriginal, self->id);
}

static VOID InstallAvatarInitPreStepLog(const PatchDescriptor *self)
{
	HOOK_SITE(
		SITE_AVATAR_INIT_PRE_STEP, AvatarInitPreStepHook, g_AvatarInitPreStepOriginal, self->id);
}

static VOID InstallAvatarInitDeviceLog(const PatchDescriptor *self)
{
	HOOK_SITE(SITE_AVATAR_INIT_DEVICE, AvatarInitDeviceHook, g_AvatarInitDeviceOriginal, self->id);
}

static VOID InstallAvatarInitAnimsLog(const PatchDescriptor *self)
{
	HOOK_SITE(SITE_AVATAR_INIT_ANIMS, AvatarInitAnimsHook, g_AvatarInitAnimsOriginal, self->id);
}

/* Early and title pass; the second run reports already. */
static const PatchDescriptor kRows[] = {
	{"avatar init log", "avatar_init_log", ON_EARLY | ON_TITLE,
		"Logs the avatar init wrapper and ready flag",
		InstallAvatarInitLog},
	{"avatar init pre-step log", "avatar_init_prestep_log", ON_EARLY | ON_TITLE,
		"Logs the step before XamAvatarInitialize", InstallAvatarInitPreStepLog},
	{"avatar init device log", "avatar_init_device_log", ON_EARLY | ON_TITLE,
		"Logs the asset-device step", InstallAvatarInitDeviceLog},
	{"avatar init anims log", "avatar_init_anims_log", ON_EARLY | ON_TITLE,
		"Logs the last avatar init step", InstallAvatarInitAnimsLog},
};
const PatchTable kAvatarInitProbePatches = PATCH_TABLE(kRows);
