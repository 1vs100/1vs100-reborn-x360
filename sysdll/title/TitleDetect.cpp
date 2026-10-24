#include "TitleDetect.h"
#include "../platform/Log.h"
#include "../platform/MemoryPatch.h"

/* One row per supported build, newest first. */
extern const TitleProfile kTitleProfile1149;
extern const TitleProfile kTitleProfile1317;
static const TitleProfile *const kProfiles[] = {
	&kTitleProfile1317,
	&kTitleProfile1149,
};

/* Written by the pass, read by title thread hooks. */
static const TitleProfile *g_Current = NULL;
static const SiteSpec *g_Sites[SITE_COUNT];
/* Mismatched read-only rows, kept so patches can report why. */
static const SiteSpec *g_Refused[SITE_COUNT];

static const TitleProfile *FindProfile(PLDR_DATA_TABLE_ENTRY module)
{
	DWORD titleId = XamGetCurrentTitleId();
	DWORD fileTime = module->TimeDateStamp;
	DWORD i = 0;
	while (i < sizeof(kProfiles) / sizeof(kProfiles[0])) {
		if (fileTime == kProfiles[i]->timeDateStamp && titleId == kProfiles[i]->titleId) {
			LOG_INFO("title", "build guard: matched %s (title=%08X filetime=%08X)",
				kProfiles[i]->name, titleId, fileTime);
			return kProfiles[i];
		}
		i++;
	}
	LOG_INFO("title", "build guard: unknown title=%08X filetime=%08X", titleId, fileTime);
	return NULL;
}

/* A mismatched read-only row is a wrong address: drop it. */
static VOID DropMismatchedReadOnlySites(const TitleProfile *profile)
{
	DWORD i = 0;
	while (i < profile->siteCount) {
		const SiteSpec *s = &profile->sites[i];
		if (s->readOnly && !BytesAt(s->address, s->expect, s->expectSize)) {
			LOG_ERROR("title",
				"profile %s: site %u at %08X does not hold its original bytes, dropped",
				profile->name, (DWORD)s->id, s->address);
			g_Sites[s->id] = NULL;
			g_Refused[s->id] = s;
		}
		i++;
	}
}

BOOL SelectTitleProfile()
{
	PLDR_DATA_TABLE_ENTRY module = (PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle;
	const TitleProfile *profile;
	const char *why = NULL;
	DWORD row = 0;

	ClearTitleProfile();
	if (module == NULL) {
		LOG_INFO("title", "build guard: title module unavailable");
		return FALSE;
	}
	profile = FindProfile(module);
	if (profile == NULL)
		return FALSE;
	if (!TitleProfile_Index(
			profile, (DWORD)module->ImageBase, module->SizeOfFullImage, g_Sites, &why, &row)) {
		LOG_ERROR("title", "profile %s refused: %s (row %u, id %u, address %08X, image %08X+%08X)",
			profile->name, why, row, (DWORD)profile->sites[row].id, profile->sites[row].address,
			(DWORD)module->ImageBase, module->SizeOfFullImage);
		ClearTitleProfile();
		return FALSE;
	}
	DropMismatchedReadOnlySites(profile);
	g_Current = profile;
	return TRUE;
}

VOID ClearTitleProfile()
{
	DWORD i = 0;
	g_Current = NULL;
	while (i < SITE_COUNT) {
		g_Sites[i] = NULL;
		g_Refused[i] = NULL;
		i++;
	}
}

const TitleProfile *CurrentTitleProfile()
{
	return g_Current;
}

const char *TitleProfileName()
{
	return g_Current ? g_Current->name : "none";
}

const char *TitlePolicyOff(const char *patch)
{
	return TitleProfile_PolicyOff(g_Current, patch);
}

DWORD TitleContentTitleId()
{
	return g_Current ? g_Current->contentTitleId : 0;
}

const SiteSpec *TitleSite(SiteId id)
{
	return (DWORD)id < SITE_COUNT ? g_Sites[id] : NULL;
}

const SiteSpec *TitleRefusedSite(SiteId id)
{
	return (DWORD)id < SITE_COUNT ? g_Refused[id] : NULL;
}

DWORD TitleSiteAddress(SiteId id)
{
	const SiteSpec *s = TitleSite(id);
	return s ? s->address : 0;
}
