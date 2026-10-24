#pragma once
#include "../stdafx.h"

/* Removes every native remote member; called by the leave handler before the leave runs. */
VOID NativeRemoveAllBeforeLeave(DWORD session);

/* TRUE while a native remote member is around. A stale read only routes one send. */
BOOL NativeRemoteMembersPresent();
