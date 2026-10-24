#include "PatchHelpers.h"
#include "../platform/Log.h"
#include "../platform/HookCave.h"
#include "../platform/ImportHook.h"

BOOL SiteMatches(const PatchSite &site)
{
	if (site.address == 0)
		return FALSE;
	return site.signature == NULL || BytesAt(site.address, site.signature, site.signatureSize);
}

BOOL SiteAccepts(
	DWORD addr, const void *original, DWORD originalSize, const void *patched, DWORD patchedSize)
{
	return BytesAt(addr, patched, patchedSize) || BytesAt(addr, original, originalSize);
}

static BOOL ReportResult(const char *name, PatchResult result, DWORD addr)
{
	switch (result) {
	case PATCH_INSTALLED:
		ReportPatch(name, "installed", addr, NULL);
		return TRUE;
	case PATCH_ALREADY:
		ReportPatch(name, "already", addr, NULL);
		return TRUE;
	case PATCH_NO_SITE:
		ReportPatch(name, "skipped", 0, "site");
		return FALSE;
	case PATCH_REFUSED:
		ReportPatch(name, "refused", addr, "signature");
		return FALSE;
	default:
		ReportPatch(name, "failed", addr, "verify");
		return FALSE;
	}
}

BOOL EnsurePatch(const char *name, DWORD addr, const void *original, DWORD originalSize,
	const void *patched, DWORD patchedSize)
{
	/* Log shows the first word; shorter patches are zero-padded. */
	BYTE before[4] = {0}, after[4] = {0};
	DWORD shown = patchedSize < 4 ? patchedSize : 4;
	PatchResult result;
	if (addr != 0)
		memcpy(before, (const void *)addr, shown);
	memcpy(after, patched, shown);
	result = EnsureBytes(addr, original, originalSize, patched, patchedSize);
	if (result == PATCH_INSTALLED)
		LOG_INFO("patch", "poke %s %08X len=%u before=%02X%02X%02X%02X after=%02X%02X%02X%02X",
			name, addr, patchedSize, before[0], before[1], before[2], before[3], after[0], after[1],
			after[2], after[3]);
	return ReportResult(name, result, addr);
}

BOOL EnsureJumpPatch(
	const char *name, DWORD addr, const void *original, DWORD originalSize, DWORD hook)
{
	PatchResult result = EnsureJump(addr, original, originalSize, hook);
	if (result == PATCH_INSTALLED)
		LOG_INFO("patch", "%s installed at %08X -> %08X", name, addr, hook);
	return ReportResult(name, result, addr);
}

BOOL ApplyPoke(const PatchSite &site, const BYTE *patch, DWORD patchSize, const char *name)
{
	return EnsurePatch(name, site.address, site.signature, site.signatureSize, patch, patchSize);
}

BOOL InstallJump(const PatchSite &site, DWORD hook, const char *name)
{
	return EnsureJumpPatch(name, site.address, site.signature, site.signatureSize, hook);
}

/* Present with expect bytes; else reports and returns NULL. */
static const SiteSpec *CheckableSite(SiteId id, const char *name)
{
	const SiteSpec *site = TitleSite(id);
	if (site == NULL) {
		ReportPatch(name, "skipped", 0, "site");
		return NULL;
	}
	if (site->expect == NULL) {
		ReportPatch(name, "refused", site->address, "no_signature");
		return NULL;
	}
	return site;
}

BOOL ApplyPokeSite(SiteId id, const void *patch, DWORD patchSize, const char *name)
{
	const SiteSpec *site = CheckableSite(id, name);
	return site != NULL &&
		EnsurePatch(name, site->address, site->expect, site->expectSize, patch, patchSize);
}

BOOL InstallJumpSite(SiteId id, DWORD hook, const char *name)
{
	const SiteSpec *site = CheckableSite(id, name);
	return site != NULL &&
		EnsureJumpPatch(name, site->address, site->expect, site->expectSize, hook);
}

BOOL HookSite(SiteId id, DWORD hook, DWORD *original, const char *name)
{
	const SiteSpec *site = CheckableSite(id, name);
	return site != NULL &&
		HookChecked(site->address, site->expect, site->expectSize, hook, original, name);
}

BOOL RequireSites(const SiteId *ids, DWORD count, const char *name)
{
	DWORD i = 0;
	while (i < count) {
		if (TitleSite(ids[i]) == NULL) {
			const SiteSpec *refused = TitleRefusedSite(ids[i]);
			if (refused != NULL)
				ReportPatch(name, "refused", refused->address, "signature");
			else
				ReportPatch(name, "skipped", 0, "site");
			return FALSE;
		}
		i++;
	}
	return TRUE;
}

BOOL PolicySkips(const char *name)
{
	const char *reason = TitlePolicyOff(name);
	if (reason == NULL)
		return FALSE;
	LOG_INFO("patch", "%s: off for %s (%s)", name, TitleProfileName(), reason);
	ReportPatch(name, "skipped", 0, "build");
	return TRUE;
}

BOOL HookChecked(DWORD addr, const BYTE *signature, DWORD signatureSize, DWORD hook,
	DWORD *original, const char *name)
{
	PatchSite site = {addr, signature, signatureSize};
	if (!SiteMatches(site)) {
		if (HookCave_IsHooked(addr)) {
			ReportPatch(name, "already", addr, NULL);
			return TRUE;
		}
		ReportPatchMismatch(name, addr);
		return FALSE;
	}
	if (!HookXamInternal(addr, hook, original, name)) {
		ReportPatch(name, "failed", addr, "hook");
		return FALSE;
	}
	ReportPatch(name, "installed", addr, NULL);
	return TRUE;
}

VOID ReportPatch(const char *name, const char *result, DWORD site, const char *why)
{
	/* Manifest line: "[patch] id=..." after the level letter. */
	LogLevel level = (result[0] == 'f') ? LOG_LEVEL_WARN : LOG_LEVEL_INFO;
	LOG_AT(level, "patch", "id=\"%s\" result=%s site=%08X why=%s", name, result, site,
		why ? why : "-");
}

VOID ReportPatchMismatch(const char *name, DWORD site)
{
	if (site == 0)
		ReportPatch(name, "skipped", 0, "site");
	else
		ReportPatch(name, "refused", site, "signature");
}

VOID ReportPatchPhase(const char *marker, const char *phase)
{
	LOG_INFO("patch", "%s phase=%s build=%s", marker, phase, TitleProfileName());
}

VOID HookXamImport(DWORD ordinal, DWORD hook, const char *name)
{
	HookModuleImport("xam.xex", ordinal, hook, name);
}

VOID HookModuleImport(const char *module, DWORD ordinal, DWORD hook, const char *name)
{
	ImportPatchResult result = PatchModuleImport(
		(PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle, (CHAR *)module, ordinal, hook);
	if (result == IMPORT_PATCHED)
		ReportPatch(name, "installed", ordinal, "import");
	else if (result == IMPORT_NO_MATCH)
		ReportPatch(name, "skipped", ordinal, "no_match");
	else
		ReportPatch(name, "failed", ordinal, "unresolved");
}
