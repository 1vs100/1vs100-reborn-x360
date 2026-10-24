#include "TitleEvents.h"
#include "../matchmaking/MatchmakingClient.h"

/* In game: on the roster. Out: others must drop the console, not keep a ghost. */
VOID TitleEvent_LocalGameEntered()
{
	MatchmakingResume();
}

VOID TitleEvent_LocalGameLeft()
{
	MatchmakingSetPlayersInGame(0);
	MatchmakingPause();
}
