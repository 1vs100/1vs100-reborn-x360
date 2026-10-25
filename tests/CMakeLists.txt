# Host-side unit tests for the pure platform code.
#   cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
cmake_minimum_required(VERSION 3.21)
project(1vs100RebornHostTests CXX)

add_executable(host-tests
    main.cpp
    test_text.cpp
    test_ppc.cpp
    test_ini.cpp
    test_format.cpp
    test_title_profile.cpp
    test_mm_protocol.cpp
    test_logconfig.cpp
    test_launcher_config.cpp
    ../sysdll/platform/Text.cpp
    ../sysdll/platform/Ppc.cpp
    ../sysdll/platform/Ini.cpp
    ../sysdll/platform/Format.cpp
    ../sysdll/platform/LogConfig.cpp
    ../sysdll/title/TitleProfile.cpp
    ../sysdll/title/TitleProfile1149.cpp
    ../sysdll/title/TitleProfile1317.cpp
    ../sysdll/matchmaking/Protocol.cpp
    ../launcher/LauncherConfig.cpp)
target_compile_options(host-tests PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME host-tests COMMAND host-tests)
