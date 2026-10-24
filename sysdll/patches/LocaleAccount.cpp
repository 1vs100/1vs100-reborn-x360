/* Account locale from the console instead of Live. Completes at once. */
#include "../stdafx.h"
#include "../platform/Log.h"
#include "../platform/ImportHook.h"
#include "PatchTables.h"
#include "../registry/PatchHelpers.h"
#include <stddef.h>

namespace
{

typedef BYTE(WINAPI *CountryFromLocale)(BYTE);
CountryFromLocale g_CountryFromLocale;

// LCID reads bCountryId at +0x38 on success.
C_ASSERT(offsetof(GET_USER_INFO_RESPONSE, bCountryId) == 0x38);
C_ASSERT(sizeof(GET_USER_INFO_RESPONSE) == 0x3C);

WORD OnlineLanguage()
{
	DWORD language = XGetLanguage();
	switch (language) {
	case XC_LANGUAGE_SWEDISH:
		return XONLINE_LANGUAGE_SWEDISH;
	case XC_LANGUAGE_TURKISH:
		return XONLINE_LANGUAGE_TURKISH;
	case XC_LANGUAGE_SCHINESE:
		return XONLINE_LANGUAGE_SCHINESE;
	default:
		return (WORD)language;
	}
}

BOOL AppendWideString(
	BYTE *buffer, DWORD capacity, DWORD *offset, const WCHAR *value, WORD *length, WCHAR **pointer)
{
	DWORD chars = 0;
	while (value[chars] != L'\0')
		++chars;
	DWORD bytes = (chars + 1) * sizeof(WCHAR);
	if (*offset > capacity || bytes > capacity - *offset || chars > 0xFFFF)
		return FALSE;

	WCHAR *destination = (WCHAR *)(buffer + *offset);
	memcpy(destination, value, bytes);
	*length = (WORD)chars;
	*pointer = destination;
	*offset += bytes;
	return TRUE;
}

DWORD WINAPI LocalAccountGetUserInfo(
	DWORD user, DWORD size, GET_USER_INFO_RESPONSE *result, XOVERLAPPED *overlapped)
{
	// Synchronous: no pending task or callback.
	if (user >= XUSER_MAX_COUNT || result == NULL)
		return ERROR_INVALID_PARAMETER;
	if (size < XAccountGetUserInfoResponseSize())
		return ERROR_INSUFFICIENT_BUFFER;
	if (XamUserGetSigninState(user) == eXamUserSigninState_NotSignedIn)
		return ERROR_NOT_LOGGED_ON;

	BYTE country = g_CountryFromLocale((BYTE)XGetLocale());
	if (country == XONLINE_COUNTRY_UNKNOWN)
		return ERROR_INVALID_DATA;

	ZeroMemory(result, sizeof(*result));
	DWORD offset = sizeof(*result);
	BYTE *buffer = (BYTE *)result;
	if (!AppendWideString(
			buffer, size, &offset, L"Player", &result->wFirstNameLength, &result->wszFirstName) ||
		!AppendWideString(
			buffer, size, &offset, L"OneVs100", &result->wLastNameLength, &result->wszLastName) ||
		!AppendWideString(buffer, size, &offset, L"1 Offline Way",
			&result->addressInfo.wStreet1Length, &result->addressInfo.wszStreet1) ||
		!AppendWideString(buffer, size, &offset, L"Apartment 100",
			&result->addressInfo.wStreet2Length, &result->addressInfo.wszStreet2) ||
		!AppendWideString(buffer, size, &offset, L"Seattle", &result->addressInfo.wCityLength,
			&result->addressInfo.wszCity) ||
		!AppendWideString(buffer, size, &offset, L"King", &result->addressInfo.wDistrictLength,
			&result->addressInfo.wszDistrict) ||
		!AppendWideString(buffer, size, &offset, L"WA", &result->addressInfo.wStateLength,
			&result->addressInfo.wszState) ||
		!AppendWideString(buffer, size, &offset, L"98052", &result->addressInfo.wPostalCodeLength,
			&result->addressInfo.wszPostalCode) ||
		!AppendWideString(buffer, size, &offset, L"player@example.invalid", &result->wEmailLength,
			&result->wszEmail))
		return ERROR_INSUFFICIENT_BUFFER;

	result->wLanguageId = OnlineLanguage();
	result->bCountryId = country;
	result->bMsftOptIn = 0;
	result->bParterOptIn = 0;
	result->bAge = 18;
	if (overlapped != NULL) {
		overlapped->InternalLow = ERROR_SUCCESS;
		overlapped->InternalHigh = 0;
		overlapped->dwExtendedError = ERROR_SUCCESS;
	}
	LOG_INFO("title", "local account locale user=%u language=%u country=%u (synchronous)", user,
		result->wLanguageId, country);
	return ERROR_SUCCESS;
}
} // namespace

static VOID InstallLocalAccountLocale(const PatchDescriptor *self)
{
	const DWORD address = TitleSiteAddress(SITE_ACCOUNT_GET_USER_INFO);
	if (TitleSite(SITE_ACCOUNT_GET_USER_INFO) == NULL) {
		ReportPatchMismatch(self->id, address);
		return;
	}
	g_CountryFromLocale = (CountryFromLocale)ResolveFunction("xam.xex", 1289);
	if (g_CountryFromLocale == NULL) {
		LOG_INFO("title", "locale hook refused: native country converter unavailable");
		ReportPatch(self->id, "failed", address, "resolve");
		return;
	}
	InstallJumpSite(SITE_ACCOUNT_GET_USER_INFO, (DWORD)LocalAccountGetUserInfo, self->id);
}

static const PatchDescriptor kRows[] = {
	{"local account locale", "account_locale", ON_TITLE,
		"The title's account/locale request completes locally from the console's locale",
		InstallLocalAccountLocale},
};
const PatchTable kLocaleAccountPatches = PATCH_TABLE(kRows);
