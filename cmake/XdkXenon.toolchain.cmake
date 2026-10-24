# Xbox 360 (Xenon / PowerPC, big-endian) — official XDK toolchain for CMake.
# Host-aware: Windows drives the XDK tools directly; Linux/WSL drives them under
# Wine via the cmake/wine/ wrappers (which translate Linux paths to Z:\ paths).
set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR ppcbe)

if(NOT DEFINED ENV{XEDK})
    message(FATAL_ERROR "XEDK is not set. Set it to the XDK root (a Windows path on Windows, a Linux/WSL path under Wine).")
endif()

file(TO_CMAKE_PATH "$ENV{XEDK}" XDK_ROOT)
set(_xdk_bin "${XDK_ROOT}/bin/win32")

if(CMAKE_HOST_WIN32)
    set(CMAKE_C_COMPILER   "${_xdk_bin}/cl.exe")
    set(CMAKE_CXX_COMPILER "${_xdk_bin}/cl.exe")
    set(CMAKE_AR           "${_xdk_bin}/lib.exe")
    set(CMAKE_LINKER       "${_xdk_bin}/link.exe")
    set(XDK_IMAGEXEX       "${_xdk_bin}/imagexex.exe" CACHE FILEPATH "XEX packager")
else()
    # Linux/Wine: the wrappers translate Linux paths -> Wine Z:\ and run the
    # tool under wine. They read XEDK/WINEPREFIX from the build environment.
    set(_wine "${CMAKE_CURRENT_LIST_DIR}/wine")
    # The wrappers are invoked directly by CMake/Ninja, so they must be
    # executable. Git carries the bit on clone, but a project scaffolded on
    # Windows (PowerShell Copy-Item) loses it — ensure it here at configure time.
    file(CHMOD
        "${_wine}/cl-wine" "${_wine}/link-wine"
        "${_wine}/lib-wine" "${_wine}/imagexex-wine"
        PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
    set(CMAKE_C_COMPILER   "${_wine}/cl-wine")
    set(CMAKE_CXX_COMPILER "${_wine}/cl-wine")
    set(CMAKE_AR           "${_wine}/lib-wine")
    set(CMAKE_LINKER       "${_wine}/link-wine")
    set(XDK_IMAGEXEX       "${_wine}/imagexex-wine" CACHE FILEPATH "XEX packager")
    find_program(NTLM_AUTH ntlm_auth)
    if(NOT NTLM_AUTH)
        message(FATAL_ERROR "ntlm_auth not found on PATH — install winbind, else the XDK link.exe fails with LNK1101 under Wine.")
    endif()
    unset(NTLM_AUTH CACHE)
endif()

# The 2010-era Xenon cl.exe confuses CMake's compiler identification/ABI probe.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER_ID   MSVC)
set(CMAKE_CXX_COMPILER_ID MSVC)
set(CMAKE_C_COMPILER_FORCED   TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

# XDK headers / import libs.
include_directories(SYSTEM "${XDK_ROOT}/include/xbox")
link_directories("${XDK_ROOT}/lib/xbox")

# Compile flags — matched to the proven xstd_test.vcxproj (C4005 on _MSC_VER expected).
add_compile_definitions(_XBOX NDEBUG _MSC_VER=1300)
add_compile_options(/c /Z7 /O2 /GR- /Gy /MT /fp:fast /TP)

set(CMAKE_FIND_ROOT_PATH "${XDK_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# NOTE: The Generic platform forces TARGET_SUPPORTS_SHARED_LIBS FALSE and GNU
# naming AFTER this toolchain runs. The MSVC conventions + build rules therefore
# live in cmake/XdkXex.cmake, include()d from the generated CMakeLists AFTER
# project(). See that file.
