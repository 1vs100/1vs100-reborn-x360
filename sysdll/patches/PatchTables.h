#pragma once
#include "../registry/PatchRegistry.h"

/* Registry tables of the patch modules; install order in core/PatchList.cpp. */
extern const PatchTable kBackendRedirectPatches;
extern const PatchTable kContentPackagePatches;
extern const PatchTable kLiveIdentityPatches;
extern const PatchTable kLivelessSessionPatches;
extern const PatchTable kLocaleAccountPatches;
extern const PatchTable kLspServerPatches;
extern const PatchTable kNetworkSecurityPatches;
extern const PatchTable kPresencePatches;
extern const PatchTable kNativeRemoteMemberPatches;
extern const PatchTable kQNetRelayPatches;
extern const PatchTable kTitleFixPatches;
