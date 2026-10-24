#pragma once
#include "../stdafx.h"

/* Interlocked* alone are no barrier on Xbox 360: acquire on take, release on free. */

struct SpinLock {
	volatile LONG v;
};

#define SPINLOCK_INIT {0}

inline BOOL SpinLock_TryLock(SpinLock *l)
{
	return InterlockedCompareExchangeAcquire(&l->v, 1, 0) == 0;
}

inline VOID SpinLock_Lock(SpinLock *l)
{
	while (InterlockedCompareExchangeAcquire(&l->v, 1, 0) != 0)
		YieldProcessor();
}

/* For locks held across slow work: yield instead of spinning. */
inline VOID SpinLock_LockSleeping(SpinLock *l)
{
	while (!SpinLock_TryLock(l))
		Sleep(0);
}

inline VOID SpinLock_Unlock(SpinLock *l)
{
	ReleaseLockBarrier();
	InterlockedExchange(&l->v, 0);
}
