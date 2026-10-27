#pragma once
#include "../stdafx.h"

/* The title's entry point waits for the title pass, so no game code runs unpatched. */

/* Early pass, title mapped and not yet entered. Repeat calls for the same title do nothing. */
VOID InstallTitleEntryHook();
/* Exited: the cave slots go with the title; forget the trampoline. */
VOID ResetTitleEntryHook();
