#include "MemoryPatch.h"
#include "Ppc.h"

BOOL BytesAt(DWORD addr, const void *bytes, DWORD n)
{
	return addr != 0 && memcmp((const void *)addr, bytes, n) == 0;
}

VOID WriteCode(DWORD addr, const void *bytes, DWORD n)
{
	MmSetAddressProtect((PVOID)addr, n, PAGE_READWRITE);
	memcpy((void *)addr, bytes, n);
	doSync((void *)addr);
}

PatchResult EnsureBytes(
	DWORD addr, const void *original, DWORD originalSize, const void *patched, DWORD patchedSize)
{
	if (addr == 0)
		return PATCH_NO_SITE;
	if (BytesAt(addr, patched, patchedSize))
		return PATCH_ALREADY;
	if (original != NULL && !BytesAt(addr, original, originalSize))
		return PATCH_REFUSED;
	WriteCode(addr, patched, patchedSize);
	return BytesAt(addr, patched, patchedSize) ? PATCH_INSTALLED : PATCH_FAILED;
}

PatchResult EnsureJump(DWORD addr, const void *original, DWORD originalSize, DWORD target)
{
	DWORD jump[4];
	Ppc_FarJump(jump, target, FALSE);
	return EnsureBytes(addr, original, originalSize, jump, sizeof(jump));
}
