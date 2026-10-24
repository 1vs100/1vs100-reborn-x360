#pragma once
#include "Types.h"

/* Branch encoding in one place so callers cannot skip the range check. */

/* `b` / `bl` from `from` to `to`. Only valid when Ppc_InBranchRange(from, to). */
DWORD Ppc_Branch(DWORD from, DWORD to, BOOL link);
/* TRUE when a relative branch reaches: -32 MB <= to - from < 32 MB. */
BOOL Ppc_InBranchRange(DWORD from, DWORD to);
/* TRUE when `word`, read at `addr`, is an unconditional relative `b` (no link, not absolute);
 * *target is then its destination. */
BOOL Ppc_DecodeBranch(DWORD addr, DWORD word, DWORD *target);
/* Beyond branch range: lis r11, hi; addi; mtctr; bctr. */
VOID Ppc_FarJump(DWORD out[4], DWORD target, BOOL link);
VOID Ppc_PutWordBE(BYTE *dst, DWORD w);
