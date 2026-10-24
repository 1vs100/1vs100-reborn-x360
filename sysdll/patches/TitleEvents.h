#pragma once
#include "../stdafx.h"

/* Local player events; TitleEvents.cpp wires the reactions. Safe from any thread. */

/* In a game. */
VOID TitleEvent_LocalGameEntered();
/* Out of the game. */
VOID TitleEvent_LocalGameLeft();
