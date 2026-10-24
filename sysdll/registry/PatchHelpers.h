#pragma once
#include "../stdafx.h"
#include "../platform/MemoryPatch.h"
#include "../title/TitleDetect.h"

/* Shared title patch helpers. Unknown builds never receive a blind write. */

struct PatchSite {
	DWORD address;
	const BYTE *signature;
	DWORD signatureSize;
};

#define CHECKED_SITE(address, signature) {address, signature, sizeof(signature)}
#define CHECKED_DWORD_SITE(address, signature) {address, (const BYTE *)signature, sizeof(signature)}

/* Title memory reads go through BytesAt, never memcmp. */

/* TRUE when the site still holds the expected bytes. */
BOOL SiteMatches(const PatchSite &site);

/* TRUE when addr holds original or patched bytes. */
BOOL SiteAccepts(
	DWORD addr, const void *original, DWORD originalSize, const void *patched, DWORD patchedSize);

/* Plus the manifest line. TRUE when the patch is in place afterwards. */
BOOL EnsurePatch(const char *name, DWORD addr, const void *original, DWORD originalSize,
	const void *patched, DWORD patchedSize);
BOOL EnsureJumpPatch(
	const char *name, DWORD addr, const void *original, DWORD originalSize, DWORD hook);

/* Same, for a PatchSite. */
BOOL ApplyPoke(const PatchSite &site, const BYTE *patch, DWORD patchSize, const char *name);
BOOL InstallJump(const PatchSite &site, DWORD hook, const char *name);

/* Same, for a title site. Missing is skipped; no expect bytes is refused. */
BOOL ApplyPokeSite(SiteId id, const void *patch, DWORD patchSize, const char *name);
BOOL InstallJumpSite(SiteId id, DWORD hook, const char *name);
BOOL HookSite(SiteId id, DWORD hook, DWORD *original, const char *name);
#define HOOK_SITE(id, hook, original, name) \
	HookSite((id), (DWORD)(hook), (DWORD *)&(original), (name))

/* TRUE when the build has every listed site; else reports the first missing one. */
BOOL RequireSites(const SiteId *ids, DWORD count, const char *name);
#define REQUIRE_SITES(ids, name) RequireSites((ids), sizeof(ids) / sizeof((ids)[0]), (name))

/* TRUE when the build policy turns `name` off. */
BOOL PolicySkips(const char *name);

/* Signature-checked HookXamInternal; TRUE only when installed. */
BOOL HookChecked(DWORD addr, const BYTE *signature, DWORD signatureSize, DWORD hook,
	DWORD *original, const char *name);

/* Title import of xam ordinal to hook. Site is the ordinal. */
VOID HookXamImport(DWORD ordinal, DWORD hook, const char *name);
/* Same, for another module. */
VOID HookModuleImport(const char *module, DWORD ordinal, DWORD hook, const char *name);

#define HOOK_CHECKED(addr, signature, hook, original, name) \
	HookChecked((addr), (signature), sizeof(signature), (DWORD)(hook), (DWORD *)&(original), (name))

/* One fixed-shape manifest line per patch site. `site` is the address (the ordinal for import hooks). */
VOID ReportPatch(const char *name, const char *result, DWORD site, const char *why);
/* 0 is skipped, else refused. */
VOID ReportPatchMismatch(const char *name, DWORD site);
/* Brackets one patch pass. */
VOID ReportPatchPhase(const char *marker, const char *phase);
