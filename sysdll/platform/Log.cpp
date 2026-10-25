#include "Log.h"

BOOL Log_Every(volatile LONG *counter, LONG n)
{
	return (InterlockedIncrement(counter) - 1) % n == 0;
}

BOOL Log_First(volatile LONG *counter, LONG n)
{
	return InterlockedIncrement(counter) <= n;
}

BOOL Log_FirstSeen(LogSeenSet *set, DWORD key)
{
	DWORD i = 0, n = set->count;
	while (i < n && i < LOG_SEEN_SLOTS) {
		if (set->keys[i] == key)
			return FALSE;
		i++;
	}
	if (n < LOG_SEEN_SLOTS) {
		set->keys[n] = key;
		set->count = n + 1;
	}
	return TRUE;
}
