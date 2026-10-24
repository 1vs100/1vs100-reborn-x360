#pragma once
#include "../stdafx.h"
#include "LaunchHandover.h"

/* 1vs100Reborn.disable holds whitespace separated patch names; a listed patch stays off. Before hand-over no patch is off. */
BOOL LoaderPatchDisabled(const char *name);
/* Called once, from SetRevivalConfig. */
VOID SetPatchSwitches(const CHAR *text);
