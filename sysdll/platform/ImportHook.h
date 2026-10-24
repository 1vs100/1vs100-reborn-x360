#pragma once
#include "../stdafx.h"


FARPROC ResolveFunction(CHAR *ModuleName, DWORD Ordinal);

enum ImportPatchResult {
	IMPORT_PATCHED,   /* at least one import slot now points at the hook */
	IMPORT_NO_MATCH,  /* no slot holds the real export */
	IMPORT_UNRESOLVED /* export or import table not found */
};
/* Only slots holding the real export are patched, so each ordinal hooks once. */
ImportPatchResult PatchModuleImport(
	PLDR_DATA_TABLE_ENTRY Module, CHAR *ImportedModuleName, DWORD Ordinal, DWORD PatchAddress);

/* Returns the original address (0 on failure). */
DWORD hookExportOrd(char *modName, DWORD ord, DWORD dstFun);

/* Only while the export still points at `hook`. TRUE when the export holds `orig`. */
BOOL unhookExportOrd(char *modName, DWORD ord, DWORD hook, DWORD orig);
