cmake_minimum_required(VERSION 3.21)
project(1vs100Reborn CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(XdkXex)

# Editor support: the XDK include path differs per machine, so .clangd is generated, not committed.
configure_file(cmake/clangd.in "${CMAKE_SOURCE_DIR}/.clangd" @ONLY)

add_subdirectory(xkelib)

# One source per line, grouped by layer (restructuring plan, "Layout & layering").
set(LOADER_SOURCES
    sysdll/stdafx.cpp
    # core: entry point and lifecycle
    sysdll/core/LoaderMain.cpp
    sysdll/core/KernelLoadHook.cpp
    sysdll/core/TitleLifecycle.cpp
    sysdll/core/PatchList.cpp
    # registry: patch manifest and checked installs
    sysdll/registry/PatchHelpers.cpp
    sysdll/registry/PatchRegistry.cpp
    # patches: one file per feature
    sysdll/patches/BackendRedirect.cpp
    sysdll/patches/ContentDirectoryScan.cpp
    sysdll/patches/ContentPackages.cpp
    sysdll/patches/LiveIdentity.cpp
    sysdll/patches/LivelessSession.cpp
    sysdll/patches/LocaleAccount.cpp
    sysdll/patches/LspServer.cpp
    sysdll/patches/NativeRemoteMembers.cpp
    sysdll/patches/NetworkSecurity.cpp
    sysdll/patches/Presence.cpp
    sysdll/patches/QNetRelay.cpp
    sysdll/patches/TitleEvents.cpp
    sysdll/patches/TitleFixes.cpp
    # matchmaking client
    sysdll/matchmaking/AvatarCache.cpp
    sysdll/matchmaking/Connection.cpp
    sysdll/matchmaking/MatchmakingClient.cpp
    sysdll/matchmaking/protocol/Protocol.cpp
    sysdll/matchmaking/RelayQueues.cpp
    sysdll/matchmaking/Roster.cpp
    # title: per-build data and detection
    sysdll/title/TitleDetect.cpp
    sysdll/title/TitleProfile.cpp
    sysdll/title/TitleProfile1149.cpp
    sysdll/title/TitleProfile1317.cpp
    # config
    sysdll/config/PatchSwitches.cpp
    sysdll/config/RevivalConfig.cpp
    # platform: no knowledge of the title
    sysdll/platform/Format.cpp
    sysdll/platform/HookCave.cpp
    sysdll/platform/ImportHook.cpp
    sysdll/platform/Ini.cpp
    sysdll/platform/Log.cpp
    sysdll/platform/LogConfig.cpp
    sysdll/platform/LogSink.cpp
    sysdll/platform/MemoryPatch.cpp
    sysdll/platform/Ppc.cpp
    sysdll/platform/Text.cpp
)

# Investigation-only hooks (restructuring plan, YAG-1): log mirrors, probes and network tracing.
# Off in the production build; the xdk-diagnostics preset turns them on.
option(LOADER_DIAGNOSTICS "Build the investigation-only hooks (diagnostics/)" OFF)
if(LOADER_DIAGNOSTICS)
    list(APPEND LOADER_SOURCES
        sysdll/diagnostics/AvatarInitProbes.cpp
        sysdll/diagnostics/BackendProbes.cpp
        sysdll/diagnostics/DebugMirrors.cpp
        sysdll/diagnostics/MainLoopWatchdog.cpp
        sysdll/diagnostics/SessionMemberProbe.cpp
        sysdll/diagnostics/NetTrace.cpp
        sysdll/diagnostics/PeerNetworkProbe.cpp
    )
endif()

add_xex(1vs100Reborn
    TYPE       DLL
    SOURCES    ${LOADER_SOURCES}
    ENTRY      _DllMainCRTStartup
    CONFIG     "${CMAKE_SOURCE_DIR}/xex.xml"
    USE_XKELIB ON
)

# CRT-touch guards
#
# Hook code runs on title threads, and a CRT call there can put this module's per-thread CRT
# state into a TLS index XUI also uses. Two layers, because neither alone catches everything:
#   - /we4995 + #pragma deprecated (sysdll/crt_guard.h): an explicit call to a known-dangerous CRT
#     function is a compile error;
#   - check-crt-calls.py: the link map's LIBCMT/LIBCPMT symbols against cmake/crt-baseline.txt,
#     which also catches helpers the compiler inserts (a 64-bit divide lowered to a libcmt call).
target_compile_options(1vs100Reborn PRIVATE "/we4995")
if(LOADER_DIAGNOSTICS)
    target_compile_definitions(1vs100Reborn PRIVATE LOADER_DIAGNOSTICS)
endif()
target_link_options(1vs100Reborn PRIVATE "/MAP:${CMAKE_BINARY_DIR}/1vs100Reborn.map" "/MAPINFO:EXPORTS")
# SetRevivalConfig, the launcher's config hand-over, exported by ordinal.
target_link_options(1vs100Reborn PRIVATE "/DEF:${CMAKE_SOURCE_DIR}/sysdll/1vs100Reborn.def")
set_property(TARGET 1vs100Reborn APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/sysdll/1vs100Reborn.def")

find_program(PYTHON3_EXECUTABLE NAMES python3 python)
if(NOT PYTHON3_EXECUTABLE)
    message(FATAL_ERROR "python3 not found - needed for cmake/check-crt-calls.py (the CRT-touch audit)")
endif()

add_custom_command(TARGET 1vs100Reborn POST_BUILD
    COMMAND "${PYTHON3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/cmake/check-crt-calls.py"
            --map "${CMAKE_BINARY_DIR}/1vs100Reborn.map"
            --baseline "${CMAKE_SOURCE_DIR}/cmake/crt-baseline.txt"
    COMMENT "check-crt-calls: auditing 1vs100Reborn.xex for new CRT touches"
    VERBATIM)

# The log formatter is a printf subset the compiler cannot check; lint the LOG_* call sites.
add_custom_command(TARGET 1vs100Reborn PRE_LINK
    COMMAND "${PYTHON3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/cmake/check-log-format.py"
            "${CMAKE_SOURCE_DIR}/sysdll"
    COMMENT "check-log-format: LOG_* format strings against their arguments"
    VERBATIM)

# Layering (restructuring plan, "Layout & layering"): platform/, title/, config/ include down only.
add_custom_command(TARGET 1vs100Reborn PRE_LINK
    COMMAND "${PYTHON3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/cmake/check-includes.py"
            "${CMAKE_SOURCE_DIR}/sysdll"
    COMMENT "check-includes: layer include rules"
    VERBATIM)

# Build differences live in title/ only (restructuring plan, R3): no build branches in patch code.
add_custom_command(TARGET 1vs100Reborn PRE_LINK
    COMMAND "${PYTHON3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/cmake/check-build-branches.py"
            "${CMAKE_SOURCE_DIR}/sysdll"
    COMMENT "check-build-branches: no build branches outside title/"
    VERBATIM)

# The patch reference is generated from the registry rows (restructuring plan 4.8): fail when stale.
add_custom_command(TARGET 1vs100Reborn PRE_LINK
    COMMAND "${PYTHON3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/cmake/gen-patch-docs.py"
            "${CMAKE_SOURCE_DIR}" --check
    COMMENT "gen-patch-docs: docs/patches.md matches the registry"
    VERBATIM)

# Real-devkit flavor: a normal title EXE (not a debugger-extension DLL) that
# chainloads the game itself, so our hooks live in a process nothing tears
# down out from under us. Also asks the console's already-resident DashLaunch
# (launch.xex, export ordinal 2 = dlaunchStartSysModule) to load the unmodified
# 1vs100Reborn.xex sysdll system-wide first, instead of requiring a manual
# launch.ini plugin definition.
add_xex(1vs100XdkLauncher
    TYPE       EXE
    SOURCES    launcher/launcher_main.cpp
               launcher/DeviceFile.cpp
               launcher/LauncherConfig.cpp
               launcher/LauncherLog.cpp
               sysdll/platform/Ini.cpp  # shared with the sysdll: the ini parser and text helpers
               sysdll/platform/Text.cpp
               sysdll/platform/Format.cpp     # LogConfig's line format
               sysdll/platform/LogConfig.cpp  # the [log] keys and the log file's line format
    CONFIG     "${CMAKE_SOURCE_DIR}/xex-launcher.xml"
    USE_XKELIB ON
    LIBRARIES  xavatar2 d3d9 xgraphics  # avatar-data pre-flight; the renderer is linked, never started
)
