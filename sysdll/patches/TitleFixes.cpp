/* Non-network title fixes: PTNTP trap nop and the devkit banner hide. */
#include "PatchTables.h"
#include "../registry/PatchHelpers.h"

/* Nop the trap; leave blr. */
static const BYTE kPtntpDebugTrapNop[4] = {0x60, 0x00, 0x00, 0x00};

static VOID InstallPtntpTrapNop(const PatchDescriptor *self)
{
	ApplyPokeSite(SITE_PTNTP_DEBUG_TRAP, kPtntpDebugTrapNop, sizeof(kPtntpDebugTrapNop), self->id);
}

/* Make the skip unconditional; the banner is never drawn. */
static const BYTE kUnsecuredBannerPatched[4] = {0x48, 0x00, 0x00, 0x74}; /* b +0x74 */

static VOID InstallBannerHide(const PatchDescriptor *self)
{
	ApplyPokeSite(SITE_UNSECURED_BANNER_BRANCH, kUnsecuredBannerPatched,
		sizeof(kUnsecuredBannerPatched), self->id);
}

static const PatchDescriptor kRows[] = {
	{"PTNTP debug trap nop", "ptntp_trap", ON_TITLE,
		"The PTNTP/XULU print sink's twui trap becomes a nop (it halts a console without a debugger)",
		InstallPtntpTrapNop},
	{"unsecured banner hide", "banner", ON_TITLE,
		"XuiShell never draws the blinking CONSOLE IS UNSECURED banner", InstallBannerHide},
};
const PatchTable kTitleFixPatches = PATCH_TABLE(kRows);
