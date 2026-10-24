# ---------------------------------------------------------------------------
# MSVC conventions + build rules for the Generic platform.
#
# CMakeGenericSystem.cmake applies GNU naming (lib*.a, ar syntax) and forces
# TARGET_SUPPORTS_SHARED_LIBS FALSE *after* the toolchain file runs, clobbering
# anything the toolchain set. This module is include()d from the generated
# CMakeLists AFTER project(), so setting them here makes them stick. These
# set() calls run at the including CMakeLists' directory scope (no PARENT_SCOPE),
# which is exactly the scope the targets are defined in.
#
# The compile/static-lib/shared-lib/exe RULE variables are guarded in CMake's
# internal modules (if(NOT ...)), so values set here win.
# ---------------------------------------------------------------------------
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)

set(CMAKE_STATIC_LIBRARY_PREFIX "")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".lib")
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll")
set(CMAKE_IMPORT_LIBRARY_PREFIX "")
set(CMAKE_IMPORT_LIBRARY_SUFFIX ".lib")
set(CMAKE_EXECUTABLE_SUFFIX     ".exe")

# MSVC-style flags for referencing libraries and library dirs (Generic defaults
# to GNU -l / -L, which the XDK link.exe rejects with LNK4044).
set(CMAKE_LINK_LIBRARY_FLAG "")
set(CMAKE_LIBRARY_PATH_FLAG "/LIBPATH:")
set(CMAKE_LINK_LIBRARY_SUFFIX ".lib")
set(CMAKE_C_LINK_LIBRARY_SUFFIX ".lib")
set(CMAKE_CXX_LINK_LIBRARY_SUFFIX ".lib")
# Generic emits "-shared" into the shared-library link flags; clear it.
set(CMAKE_SHARED_LIBRARY_CXX_FLAGS "")
set(CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS "")
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

# Header dependencies: the Generic platform makes Ninja expect a gcc-style depfile, which cl.exe
# never writes, so no header was tracked and an edited header did not rebuild its users (stale
# objects linked into the XEX). Use Ninja's msvc mode instead: cl prints every include with
# /showIncludes and Ninja parses it from stdout (cmake/wine/cl-wine maps the Wine paths back).
# (Same settings Platform/Windows-MSVC.cmake makes for a real MSVC host.)
set(CMAKE_DEPFILE_FLAGS_CXX "/showIncludes")
set(CMAKE_DEPFILE_FLAGS_C "/showIncludes")
set(CMAKE_CXX_DEPFILE_FORMAT msvc)
set(CMAKE_C_DEPFILE_FORMAT msvc)
set(CMAKE_CL_SHOWINCLUDES_PREFIX "Note: including file: ")

# Drive the XDK cl.exe / lib.exe / link.exe with MSVC syntax (not GNU -o / ar).
set(CMAKE_CXX_COMPILE_OBJECT
    "<CMAKE_CXX_COMPILER> /nologo <DEFINES> <INCLUDES> <FLAGS> /c <SOURCE> /Fo<OBJECT>")
set(CMAKE_C_COMPILE_OBJECT
    "<CMAKE_C_COMPILER> /nologo <DEFINES> <INCLUDES> <FLAGS> /c <SOURCE> /Fo<OBJECT>")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY
    "<CMAKE_AR> /nologo /LTCG /OUT:<TARGET> <OBJECTS>")
set(CMAKE_C_CREATE_STATIC_LIBRARY
    "<CMAKE_AR> /nologo /LTCG /OUT:<TARGET> <OBJECTS>")
set(CMAKE_CXX_CREATE_SHARED_LIBRARY
    "<CMAKE_LINKER> /nologo /LTCG <LINK_FLAGS> <OBJECTS> /OUT:<TARGET> /IMPLIB:<TARGET_IMPLIB> <LINK_LIBRARIES>")
set(CMAKE_CXX_LINK_EXECUTABLE
    "<CMAKE_LINKER> /nologo /LTCG <LINK_FLAGS> <OBJECTS> /OUT:<TARGET> <LINK_LIBRARIES>")

# Capture this module's directory now (before function() changes CMAKE_CURRENT_LIST_DIR
# to the caller's directory at invocation time). Used by add_xex to locate verify-xex.cmake.
set(_XDKXEX_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "XdkXex.cmake directory")

# add_xex(target TYPE <DLL|EXE> SOURCES ... [ENTRY sym] CONFIG xml
#         [USE_XKELIB ON|OFF] [XKELIB_DIR dir])
# Builds the PE target and packages it into <target>.xex via imagexex.
function(add_xex target)
    cmake_parse_arguments(XEX "" "TYPE;ENTRY;CONFIG;USE_XKELIB;XKELIB_DIR" "SOURCES;LIBRARIES" ${ARGN})

    if(NOT XEX_TYPE)
        message(FATAL_ERROR "add_xex(${target}): TYPE (DLL|EXE) is required")
    endif()
    if(NOT XEX_CONFIG)
        message(FATAL_ERROR "add_xex(${target}): CONFIG <Application.xml> is required")
    endif()

    if(XEX_TYPE STREQUAL "DLL")
        set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)
        add_library(${target} SHARED ${XEX_SOURCES})
        if(NOT XEX_ENTRY)
            message(FATAL_ERROR "add_xex(${target}): DLL requires ENTRY <symbol>")
        endif()
        target_link_options(${target} PRIVATE "/DLL" "/ENTRY:${XEX_ENTRY}")
    elseif(XEX_TYPE STREQUAL "EXE")
        add_executable(${target} ${XEX_SOURCES})
        # A title XEX must declare the XBOX subsystem (correct on both hosts).
        target_link_options(${target} PRIVATE "/SUBSYSTEM:XBOX")
    else()
        message(FATAL_ERROR "add_xex(${target}): unknown TYPE '${XEX_TYPE}'")
    endif()

    # The XDK link.exe emits a XEX by DEFAULT. Force a plain PE with /XEX:NO so the
    # imagexex POST_BUILD step receives a PE to convert — otherwise imagexex is fed
    # an already-XEX file and emits a broken double-wrapped image that crashes on
    # load (image not rebased to its load address, not compressed).
    # /FIXED:NO keeps the .reloc table so imagexex can rebase to the load address.
    # /ALIGN:128,4096 (section:file) compacts DLL plugins; EXE titles omit it —
    # imagexex IM1038 fires when the PE section alignment is below the 64K XEX
    # base-address granularity required for title images.
    if(XEX_TYPE STREQUAL "DLL")
        target_link_options(${target} PRIVATE "/ALIGN:128,4096")
    endif()
    target_link_options(${target} PRIVATE "/FIXED:NO" "/XEX:NO")

    if(XEX_USE_XKELIB STREQUAL "ON")
        target_link_libraries(${target} PRIVATE xkelib)
    endif()

    # Base Xbox 360 import libraries every XEX needs, plus any caller extras.
    # Resolved from the XDK lib\xbox dir on the link path (set by the toolchain).
    target_link_libraries(${target} PRIVATE xapilib xnet xboxkrnl ${XEX_LIBRARIES})

    # imagexex parses /IN:<path> as a single token and chokes on embedded quotes,
    # so do NOT quote inside the option. VERBATIM keeps the argument intact; the
    # generated XEX lands next to the linker output in the build dir.
    set(_xex "${CMAKE_BINARY_DIR}/${target}.xex")
    # The XEX is produced at link time, so an edited CONFIG must relink the target.
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${XEX_CONFIG}")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${XDK_IMAGEXEX}" /IN:$<TARGET_FILE:${target}> /OUT:${_xex} /CONFIG:${XEX_CONFIG}
        COMMAND "${CMAKE_COMMAND}" -DXEX=${_xex} -DXEX_TYPE=${XEX_TYPE}
                -DXDK_IMAGEXEX=${XDK_IMAGEXEX}
                -P "${_XDKXEX_MODULE_DIR}/verify-xex.cmake"
        BYPRODUCTS "${_xex}"
        COMMENT "imagexex + verify -> ${target}.xex"
        VERBATIM)
endfunction()
