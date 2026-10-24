#pragma once
#include "../stdafx.h"

/* Title patch passes and reset on exit. */

enum ApplyResult {
	APPLY_UNSUPPORTED, /* not a supported build */
	APPLY_APPLIED,     /* pass completed */
	APPLY_BUSY,        /* another pass running; call again */
};

/* Main pass: target image loaded, or title changed under the poll. */
ApplyResult ApplyTitlePatches();
/* Early pass: title thread, before game code. */
VOID ApplyEarlyTitlePatches();
/* Running step, cleared on read; for the loader fault handler. */
const char *TakeTitlePatchStep();
/* Exited: forget everything tied to the old process. */
VOID ResetTitleState(DWORD exitedTitleId);
