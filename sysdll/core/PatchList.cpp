#include "../patches/PatchTables.h"
#ifdef LOADER_DIAGNOSTICS
#include "../diagnostics/DiagnosticTables.h"
#endif

/* Tables run in order; rows run in order. Diagnostics build only where marked. */
const PatchTable *const kPatchTables[] = {
#ifdef LOADER_DIAGNOSTICS
	&kAvatarInitProbePatches,
	&kSessionMemberProbePatches,
	&kPeerNetworkProbePatches,
#endif
	&kNetworkSecurityPatches,
	&kLiveIdentityPatches,
	&kPresencePatches,
	&kLspServerPatches,
#ifdef LOADER_DIAGNOSTICS
	&kNetTracePatches,
#endif
	&kBackendRedirectPatches,
#ifdef LOADER_DIAGNOSTICS
	&kBackendProbePatches,
#endif
	&kLocaleAccountPatches,
	&kLivelessSessionPatches,
	&kNativeRemoteMemberPatches,
	&kQNetRelayPatches,
	&kTitleFixPatches,
	&kContentPackagePatches,
#ifdef LOADER_DIAGNOSTICS
	&kDebugMirrorPatches,
#endif
};
const DWORD kPatchTableCount = sizeof(kPatchTables) / sizeof(kPatchTables[0]);
