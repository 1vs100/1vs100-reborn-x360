#include "TitleProfile.h"

static BOOL SameId(const char *a, const char *b)
{
	while (*a != 0 && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

const char *TitleProfile_PolicyOff(const TitleProfile *profile, const char *patch)
{
	DWORD i = 0;
	while (profile != 0 && i < profile->policyOffCount) {
		if (SameId(profile->policyOff[i].patch, patch))
			return profile->policyOff[i].reason;
		i++;
	}
	return 0;
}

BOOL TitleProfile_Index(const TitleProfile *profile, DWORD imageBase, DWORD imageSize,
	const SiteSpec **index, const char **why, DWORD *row)
{
	DWORD i = 0;
	while (i < SITE_COUNT) {
		index[i] = 0;
		i++;
	}
	i = 0;
	while (i < profile->siteCount) {
		const SiteSpec *s = &profile->sites[i];
		*row = i;
		if ((DWORD)s->id >= SITE_COUNT) {
			*why = "site id out of range";
			return FALSE;
		}
		if (index[s->id] != 0) {
			*why = "site listed twice";
			return FALSE;
		}
		if (imageSize != 0 && (s->address < imageBase || s->address - imageBase >= imageSize)) {
			*why = "address outside the title image";
			return FALSE;
		}
		index[s->id] = s;
		i++;
	}
	return TRUE;
}
