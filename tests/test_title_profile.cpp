#include "check.h"
#include "../sysdll/title/TitleProfile.h"

extern const TitleProfile kTitleProfile1149;
extern const TitleProfile kTitleProfile1317;

static const DWORD kImageBase = 0x82000000u;
static const DWORD kImageSize = 0x00BF0000u;

static void CheckProfile(const TitleProfile *p)
{
	const SiteSpec *index[SITE_COUNT];
	const char *why = 0;
	DWORD row = 0, i, present = 0;
	CHECK(TitleProfile_Index(p, kImageBase, kImageSize, index, &why, &row));
	for (i = 0; i < SITE_COUNT; i++) {
		if (index[i] == 0)
			continue;
		present++;
		CHECK(index[i]->id == (SiteId)i);
		CHECK(index[i]->address != 0);
		CHECK((index[i]->expect == 0) == (index[i]->expectSize == 0));
		if (index[i]->readOnly) /* checked when the profile is selected: needs its bytes */
			CHECK(index[i]->expect != 0);
	}
	CHECK_EQ_HEX(present, p->siteCount);
}

void TestTitleProfile()
{
	CheckProfile(&kTitleProfile1149);
	CheckProfile(&kTitleProfile1317);
	CHECK_EQ_HEX(kTitleProfile1149.titleId, 0x584A07D1u);
	CHECK_EQ_HEX(kTitleProfile1317.titleId, 0x584A07E4u);
	CHECK_EQ_HEX(kTitleProfile1317.contentTitleId, 0x584A07D1u);

	/* policy: content fixes are 1317's, the Grid trace is 1149's */
	CHECK(TitleProfile_PolicyOff(&kTitleProfile1149, "ContentId DeviceId fix") != 0);
	CHECK(TitleProfile_PolicyOff(&kTitleProfile1317, "ContentId DeviceId fix") == 0);
	CHECK(TitleProfile_PolicyOff(&kTitleProfile1317, "GridTrace") != 0);
	CHECK(TitleProfile_PolicyOff(&kTitleProfile1149, "GridTrace") == 0);
	CHECK(TitleProfile_PolicyOff(&kTitleProfile1149, "ContentId") == 0); /* whole ids only */
	CHECK(TitleProfile_PolicyOff(0, "GridTrace") == 0);

	/* self-check refusals */
	{
		static const BYTE sig[4] = {1, 2, 3, 4};
		const SiteSpec *index[SITE_COUNT];
		const char *why = 0;
		DWORD row = 0;
		const SiteSpec twice[] = {
			SITE_ADDR(SITE_MAIN_PUMP, 0x82100000u), SITE(SITE_MAIN_PUMP, 0x82100004u, sig)};
		const SiteSpec outside[] = {SITE_ADDR(SITE_MAIN_PUMP, 0x82BF0000u)};
		const SiteSpec range[] = {{SITE_COUNT, 0x82100000u, 0, 0, FALSE}};
		const SiteSpec kinds[] = {SITE(SITE_MAIN_PUMP, 0x82100000u, sig),
			SITE_REF(SITE_POST_NET_EVENT, 0x82100010u, sig),
			SITE_ADDR(SITE_TEXT_PADDING_CAVE, 0x82100020u)};
		CHECK(!kinds[0].readOnly && kinds[1].readOnly && !kinds[2].readOnly);
		CHECK(kinds[1].expectSize == sizeof(sig));
		TitleProfile p = {"test", 1, 2, 3, twice, 2, 0, 0};
		CHECK(!TitleProfile_Index(&p, kImageBase, kImageSize, index, &why, &row));
		CHECK_EQ_HEX(row, 1);
		p.sites = outside;
		p.siteCount = 1;
		CHECK(!TitleProfile_Index(&p, kImageBase, kImageSize, index, &why, &row));
		CHECK(
			TitleProfile_Index(&p, kImageBase, 0, index, &why, &row)); /* size unknown: no bound */
		p.sites = range;
		CHECK(!TitleProfile_Index(&p, kImageBase, kImageSize, index, &why, &row));
	}
}
