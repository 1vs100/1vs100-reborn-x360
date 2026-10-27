#include "ImportHook.h"
#include "Ppc.h"

FARPROC ResolveFunction(CHAR *ModuleName, DWORD Ordinal)
{
	HMODULE mHandle = GetModuleHandle(ModuleName);
	return (mHandle == NULL) ? NULL : GetProcAddress(mHandle, (LPCSTR)Ordinal);
}

static VOID PatchInJump(DWORD *addr, DWORD dest, BOOL linked)
{
	MmSetAddressProtect(addr, 16, PAGE_READWRITE);
	Ppc_FarJump(addr, dest, linked);
	doSync(addr);
}

/* Every import slot of Module holding `from` is pointed at `to`, thunk included. */
static ImportPatchResult RetargetModuleImport(
	PLDR_DATA_TABLE_ENTRY Module, DWORD from, DWORD to)
{
	VOID *headerBase = Module->XexHeaderBase;
	PXEX_IMPORT_DESCRIPTOR importDesc =
		(PXEX_IMPORT_DESCRIPTOR)RtlImageXexHeaderField(headerBase, 0x000103FF);
	if (importDesc == NULL)
		return IMPORT_UNRESOLVED;

	ImportPatchResult result = IMPORT_NO_MATCH;

	CHAR *stringTable = (CHAR *)(importDesc + 1);

	XEX_IMPORT_TABLE_ORG *importTable =
		(XEX_IMPORT_TABLE_ORG *)(stringTable + importDesc->NameTableSize);

	for (DWORD x = 0; x < importDesc->ModuleCount; x++) {
		DWORD *importAdd = (DWORD *)(importTable + 1);
		for (DWORD y = 0; y < importTable->ImportTable.ImportCount; y++) {
			DWORD value = *((DWORD *)importAdd[y]);
			if (value == from) {
				memcpy((DWORD *)importAdd[y], &to, 4);
				PatchInJump((DWORD *)importAdd[y + 1], to, FALSE);
				result = IMPORT_PATCHED;
			}
		}
		importTable = (XEX_IMPORT_TABLE_ORG *)(((BYTE *)importTable) + importTable->TableSize);
	}
	return result;
}

ImportPatchResult PatchModuleImport(
	PLDR_DATA_TABLE_ENTRY Module, CHAR *ImportedModuleName, DWORD Ordinal, DWORD PatchAddress)
{
	DWORD address = (DWORD)ResolveFunction(ImportedModuleName, Ordinal);
	if (address == NULL)
		return IMPORT_UNRESOLVED;
	return RetargetModuleImport(Module, address, PatchAddress);
}

ImportPatchResult RestoreModuleImport(
	PLDR_DATA_TABLE_ENTRY Module, CHAR *ImportedModuleName, DWORD Ordinal, DWORD PatchAddress)
{
	DWORD address = (DWORD)ResolveFunction(ImportedModuleName, Ordinal);
	if (address == NULL)
		return IMPORT_UNRESOLVED;
	return RetargetModuleImport(Module, PatchAddress, address);
}

static PIMAGE_EXPORT_ADDRESS_TABLE getModuleEat(char *modName)
{
	PLDR_DATA_TABLE_ENTRY moduleHandle = (PLDR_DATA_TABLE_ENTRY)GetModuleHandle(modName);
	if (moduleHandle != NULL) {
		PIMAGE_DATA_DIRECTORY pddir;
		PIMAGE_XEX_HEADER xhead = (PIMAGE_XEX_HEADER)moduleHandle->XexHeaderBase;
		pddir = (PIMAGE_DATA_DIRECTORY)RtlImageXexHeaderField(xhead, XEX_HEADER_PE_EXPORTS);
		if (pddir == NULL) {
			PXEX_SECURITY_INFO xs = (PXEX_SECURITY_INFO)(xhead->SecurityInfo);
			return (PIMAGE_EXPORT_ADDRESS_TABLE)xs->ImageInfo.ExportTableAddress;
		}
	}
	return NULL;
}

DWORD hookExportOrd(char *modName, DWORD ord, DWORD dstFun)
{
	if (dstFun == 0)
		return 0;
	{
		PIMAGE_EXPORT_ADDRESS_TABLE expbase = getModuleEat(modName);
		if (expbase != NULL) {
			DWORD origOffset = (expbase->ordOffset[ord - 1]);
			if (origOffset != 0) {
				DWORD modOffset = (expbase->ImageBaseAddress) << 16;
				origOffset += modOffset;
				expbase->ordOffset[ord - 1] = dstFun - modOffset;
				doSync(&expbase->ordOffset[ord - 1]);
			}
			return origOffset;
		}
	}
	return 0;
}

BOOL unhookExportOrd(char *modName, DWORD ord, DWORD hook, DWORD orig)
{
	PIMAGE_EXPORT_ADDRESS_TABLE expbase;
	DWORD modOffset, current;
	if (hook == 0 || orig == 0)
		return FALSE;
	expbase = getModuleEat(modName);
	if (expbase == NULL)
		return FALSE;
	modOffset = (expbase->ImageBaseAddress) << 16;
	current = expbase->ordOffset[ord - 1] + modOffset;
	if (current == orig)
		return TRUE;
	if (current != hook)
		return FALSE;
	expbase->ordOffset[ord - 1] = orig - modOffset;
	doSync(&expbase->ordOffset[ord - 1]);
	return TRUE;
}
