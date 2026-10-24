#pragma once
#include "TitleProfile.h"

/* Title id + module timestamp select a profile. Until selected every site is absent. */

/* TRUE for a supported build that passes its self-check. */
BOOL SelectTitleProfile();
VOID ClearTitleProfile();

/* NULL until SelectTitleProfile succeeded. */
const TitleProfile *CurrentTitleProfile();
/* "v2.0.1149.0", "v2.0.1317.0" or "none". */
const char *TitleProfileName();

/* Why `patch` is off for this build; NULL when on. */
const char *TitlePolicyOff(const char *patch);
/* Marketplace content title id; 0 without a profile. */
DWORD TitleContentTitleId();

/* Row for `id`; NULL when absent or mismatched. */
const SiteSpec *TitleSite(SiteId id);
/* Dropped read-only row for `id`; NULL otherwise. */
const SiteSpec *TitleRefusedSite(SiteId id);
/* Address of `id`; 0 when absent. */
DWORD TitleSiteAddress(SiteId id);
