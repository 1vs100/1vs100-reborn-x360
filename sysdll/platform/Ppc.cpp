#include "Ppc.h"

DWORD Ppc_Branch(DWORD from, DWORD to, BOOL link)
{
	return 0x48000000u | ((to - from) & 0x03FFFFFCu) | (link ? 1u : 0u);
}

BOOL Ppc_InBranchRange(DWORD from, DWORD to)
{
	int dist = (int)(to - from);
	return dist < 0x02000000 && dist >= -(0x02000000);
}

BOOL Ppc_DecodeBranch(DWORD addr, DWORD word, DWORD *target)
{
	DWORD offset;
	if ((word & 0xFC000003u) != 0x48000000u)
		return FALSE;
	offset = word & 0x03FFFFFCu;
	if (offset & 0x02000000u)
		offset |= 0xFC000000u; /* sign-extend the 26-bit displacement */
	*target = addr + offset;
	return TRUE;
}

VOID Ppc_FarJump(DWORD out[4], DWORD target, BOOL link)
{
	/* addi sign-extends: round the high half up when bit 15 is set. */
	DWORD high = (target >> 16) + ((target & 0x8000) ? 1 : 0);
	out[0] = 0x3D600000 | (high & 0xFFFF);
	out[1] = 0x396B0000 + (target & 0xFFFF);
	out[2] = 0x7D6903A6;
	out[3] = link ? 0x4E800421 : 0x4E800420;
}

VOID Ppc_PutWordBE(BYTE *dst, DWORD w)
{
	dst[0] = (BYTE)(w >> 24);
	dst[1] = (BYTE)(w >> 16);
	dst[2] = (BYTE)(w >> 8);
	dst[3] = (BYTE)w;
}
