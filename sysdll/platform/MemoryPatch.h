#pragma once
#include "Types.h"

/* Only place that compares or writes code memory. No logging here; callers report the PatchResult. */

enum PatchResult {
	PATCH_INSTALLED, /* expected bytes found, patch written and verified */
	PATCH_ALREADY,   /* patch already in place */
	PATCH_NO_SITE,   /* address 0: no such site in this build */
	PATCH_REFUSED,   /* unknown bytes, nothing written */
	PATCH_FAILED     /* written bytes read back wrong */
};

/* TRUE when the n bytes at addr match; FALSE for addr 0. */
BOOL BytesAt(DWORD addr, const void *bytes, DWORD n);

/* No check: use the Ensure functions. */
VOID WriteCode(DWORD addr, const void *bytes, DWORD n);

/* Idempotent; NULL original skips the check. Longer original pins trailing bytes. */
PatchResult EnsureBytes(
	DWORD addr, const void *original, DWORD originalSize, const void *patched, DWORD patchedSize);

/* Installed state is read from memory; no per-hook state is kept. */
PatchResult EnsureJump(DWORD addr, const void *original, DWORD originalSize, DWORD target);
