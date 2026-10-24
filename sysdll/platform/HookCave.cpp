#include "HookCave.h"
#include "ImportHook.h"
#include "Ppc.h"
#include "SpinLock.h"
#include "Log.h"

static VOID WriteFarJump(DWORD *addr, DWORD dest, BOOL linked)
{
	Ppc_FarJump(addr, dest, linked);
	doSync(addr);
}

/* Far-jump slots in xam trailing zeros, in branch range of xam. */
static DWORD g_xamCaveStart = 0;
static DWORD g_xamCaveCur = 0;
static DWORD g_xamCaveEnd = 0;
static DWORD g_xamCaveHooks = 0;
/* Owner of the used slots; 0 is none. */
static DWORD g_xamCaveOwner = 0;
/* Rare path, one lock for all of it. Sleeping: the first allocation scans and probes. */
static SpinLock g_xamCaveLock = SPINLOCK_INIT;

/* Caller holds the lock. Only for slots whose title is gone. */
static VOID ZeroUsedSlotsLocked()
{
	if (g_xamCaveStart && g_xamCaveCur > g_xamCaveStart) {
		DWORD used = g_xamCaveCur - g_xamCaveStart;
		__try {
			for (DWORD a = g_xamCaveStart; a < g_xamCaveCur; a += 4)
				*(volatile DWORD *)a = 0;
			for (DWORD a = g_xamCaveStart; a < g_xamCaveCur; a += 4)
				doSync((PVOID)a);
			LOG_INFO("cave", "xam cave %08X: %u bytes of title %08X zeroed", g_xamCaveStart, used,
				g_xamCaveOwner);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			LOG_ERROR("cave", "xam cave %08X: zeroing faulted", g_xamCaveStart);
		}
	}
	g_xamCaveCur = g_xamCaveStart;
	g_xamCaveHooks = 0;
	g_xamCaveOwner = 0;
}

VOID ReleaseTitleCaves(DWORD titleId)
{
	SpinLock_LockSleeping(&g_xamCaveLock);
	if (g_xamCaveOwner == titleId)
		ZeroUsedSlotsLocked();
	SpinLock_Unlock(&g_xamCaveLock);
}

VOID ReleaseXamCaves()
{
	SpinLock_LockSleeping(&g_xamCaveLock);
	ZeroUsedSlotsLocked();
	g_xamCaveStart = 0;
	g_xamCaveCur = 0;
	g_xamCaveEnd = 0;
	SpinLock_Unlock(&g_xamCaveLock);
}

BOOL HookCave_IsHooked(DWORD addr)
{
	DWORD target;
	BOOL inside;
	if (addr == 0 || !Ppc_DecodeBranch(addr, *(volatile DWORD *)addr, &target))
		return FALSE;
	SpinLock_LockSleeping(&g_xamCaveLock);
	inside = g_xamCaveStart != 0 && target >= g_xamCaveStart && target < g_xamCaveEnd;
	SpinLock_Unlock(&g_xamCaveLock);
	return inside;
}

VOID LogXamCaveUsage()
{
	SpinLock_LockSleeping(&g_xamCaveLock);
	LOG_INFO("cave", "hookcave used=%u/%u bytes hooks=%u", g_xamCaveCur - g_xamCaveStart,
		g_xamCaveEnd - g_xamCaveStart, g_xamCaveHooks);
	SpinLock_Unlock(&g_xamCaveLock);
}

/* Zero reads do not prove writable: probe with a guarded write. */
static BOOL ProbeCaveWritable(DWORD addr)
{
	BOOL ok = TRUE;
	MmSetAddressProtect((PVOID)addr, 4, 0x40);
	__try {
		volatile DWORD *p = (volatile DWORD *)addr;
		DWORD saved = *p;
		*p = 0xDEADBEEF;
		doSync((PVOID)addr);
		if (*p != 0xDEADBEEF)
			ok = FALSE;
		*p = saved;
		doSync((PVOID)addr);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ok = FALSE;
	}
	return ok;
}

/* Plain xam code writes fault: use xbdm DmSetMemory, guarded direct write as fallback. */
typedef LONG(NTAPI *DmSetMemory_t)(PVOID, DWORD, const void *, DWORD *);

static BOOL GuardedWriteDword(DWORD addr, DWORD value)
{
	static DmSetMemory_t pDmSetMemory = NULL;
	static BOOL resolved = FALSE;
	if (!resolved) {
		pDmSetMemory = (DmSetMemory_t)ResolveFunction("xbdm.xex", 40);
		resolved = TRUE;
		LOG_INFO("cave", "DmSetMemory resolved=%p", pDmSetMemory);
	}
	if (pDmSetMemory) {
		DWORD written = 0;
		LONG hr = pDmSetMemory((PVOID)addr, sizeof(DWORD), &value, &written);
		if (hr >= 0 && written == sizeof(DWORD))
			return TRUE;
		LOG_INFO("cave", "DmSetMemory(%08X) hr=%08X written=%u, falling back to direct write", addr,
			hr, written);
	}
	{
		BOOL ok = TRUE;
		__try {
			*(volatile DWORD *)addr = value;
			doSync((void *)addr);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			ok = FALSE;
		}
		return ok;
	}
}

static DWORD AllocXamCaveLocked(DWORD bytes)
{
	PLDR_DATA_TABLE_ENTRY xam;
	DWORD base, size, i, need;
	BYTE *p;
	if (!g_xamCaveCur) {
		xam = (PLDR_DATA_TABLE_ENTRY)GetModuleHandle("xam.xex");
		if (!xam)
			return 0;
		base = (DWORD)xam->ImageBase;
		size = xam->SizeOfFullImage;
		if (!base || !size)
			return 0;
		p = (BYTE *)base;
		/* 24 bytes per hook. */
		need = 0x400;
		{
			/* Scan from the end; reads are safe, the probe gates acceptance. */
			DWORD run = 0;
			DWORD scan_from = size;
			DWORD scan_to = 0;
			/* At most 32 probes; a miss skips the rest of its page. */
			DWORD probe_budget = 32;
			for (i = scan_from; i-- > scan_to;) {
				if (p[i] == 0) {
					run++;
					if (run >= need) {
						DWORD candidate = base + i;
						if (probe_budget == 0) {
							LOG_INFO("cave", "xam cave: probe budget exhausted, giving up");
							break;
						}
						probe_budget--;
						if (ProbeCaveWritable(candidate)) {
							g_xamCaveStart = g_xamCaveCur = candidate;
							g_xamCaveEnd = g_xamCaveCur + need;
							MmSetAddressProtect((PVOID)g_xamCaveCur, need, 0x40);
							LOG_INFO("cave", "xam cave %08X-%08X", g_xamCaveCur, g_xamCaveEnd);
							break;
						}
						LOG_INFO("cave",
							"xam cave candidate %08X reads zero but isn't writable, skipping",
							candidate);
						{
							DWORD page_off = i & ~0xFFFu;
							if (page_off < i)
								i = page_off;
						}
						run = 0;
					}
				} else {
					run = 0;
				}
			}
		}
	}
	if (!g_xamCaveCur)
		return 0;
	{
		/* Owner exited without release: reclaim. */
		DWORD titleId = XamGetCurrentTitleId();
		if (g_xamCaveOwner != 0 && g_xamCaveOwner != titleId)
			ZeroUsedSlotsLocked();
		g_xamCaveOwner = titleId;
	}
	if (g_xamCaveCur + bytes > g_xamCaveEnd)
		return 0;
	{
		DWORD slot = g_xamCaveCur;
		g_xamCaveCur += bytes;
		return slot;
	}
}

static DWORD AllocXamCave(DWORD bytes)
{
	DWORD slot = 0;
	SpinLock_LockSleeping(&g_xamCaveLock);
	/* Scan faults propagate; the lock must not stay taken. */
	__try {
		slot = AllocXamCaveLocked(bytes);
	} __finally {
		SpinLock_Unlock(&g_xamCaveLock);
	}
	return slot;
}

/* No export ordinal: reached only by direct bl from xam code. */
BOOL HookXamInternal(DWORD addr, DWORD hook, DWORD *pOrig, const char *name)
{
	DWORD cave;
	if (!addr) {
		LOG_INFO("cave", "xam %s addr=0", name);
		return FALSE;
	}
	cave = AllocXamCave(16);
	if (!cave || !Ppc_InBranchRange(addr, cave)) {
		LOG_INFO("cave", "xam %s orig=%08X no cave", name, addr);
		return FALSE;
	}
	{
		DWORD tslot = AllocXamCave(8);
		if (!tslot || !Ppc_InBranchRange(tslot + 4, addr + 4)) {
			LOG_INFO("cave", "xam %s tramp cave fail", name);
			return FALSE;
		}
		*(DWORD *)tslot = *(DWORD *)addr;
		*((DWORD *)tslot + 1) = Ppc_Branch(tslot + 4, addr + 4, FALSE);
		doSync((void *)tslot);
		if (pOrig)
			*pOrig = tslot;
	}
	WriteFarJump((DWORD *)cave, hook, FALSE);
	MmSetAddressProtect((PVOID)addr, 4, PAGE_READWRITE);
	if (!GuardedWriteDword(addr, Ppc_Branch(addr, cave, FALSE))) {
		LOG_INFO("cave",
			"xam %s addr=%08X not writable on this kernel (needs RGLoader/relaxed HV protection)",
			name, addr);
		return FALSE;
	}
	LOG_INFO("cave", "xam %s addr=%08X cave=%08X tramp=%08X hook=%08X", name, addr, cave,
		pOrig ? *pOrig : 0, hook);
	InterlockedIncrement((LONG volatile *)&g_xamCaveHooks);
	return TRUE;
}
