# EmuBuddy Launcher: a front end for EmuBuddy with profiles and skins.
# Plan: ../docs/LAUNCHER_PLAN.md
#
# Built on its own (cmake -S launcher) or as part of EmuBuddy's CMake project
# (emubuddy/src adds this folder), in which case it ships in the same download.
cmake_minimum_required(VERSION 3.20)
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()
project(emubuddy-launcher VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
if(DEFINED GSPLUS_VERSION AND NOT GSPLUS_VERSION STREQUAL "")
    set(EMUBUDDY_VERSION "${GSPLUS_VERSION}")
elseif(NOT DEFINED EMUBUDDY_VERSION OR EMUBUDDY_VERSION STREQUAL "")
    set(EMUBUDDY_VERSION "${PROJECT_VERSION}")
endif()

# profile.c has no SDL; it is tested on its own.
add_library(launcher_profile STATIC src/profile.c)
set(EB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../emubuddy/src)
add_library(launcher_library STATIC src/library.c ${EB_SRC}/prodos_img.c)
target_include_directories(launcher_library PUBLIC src ${EB_SRC})
target_link_libraries(launcher_library PUBLIC launcher_profile)
add_executable(test_library EXCLUDE_FROM_ALL tests/test_library.c)
target_link_libraries(test_library PRIVATE launcher_library)
add_executable(test_profile EXCLUDE_FROM_ALL tests/test_profile.c)
target_link_libraries(test_profile PRIVATE launcher_profile)
enable_testing()
add_test(NAME profile COMMAND test_profile)
add_test(NAME library COMMAND test_library)

if(NOT TARGET SDL3::SDL3)
    find_package(SDL3 CONFIG QUIET)
endif()
if(NOT TARGET SDL3::SDL3)
    include(FetchContent)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.10
        GIT_SHALLOW TRUE)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
endif()

# The window uses EmuBuddy's own UI toolkit and font, compiled from its folder.
set(EB_UI ${CMAKE_CURRENT_SOURCE_DIR}/../emubuddy/src)
set(LAUNCHER_SOURCES src/main.c src/skins.c ${EB_UI}/ui.c ${EB_UI}/ui_font.c ${EB_UI}/ui_font_data.c)
set(LAUNCHER_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../emubuddy/lib)

if(APPLE)
    set(LAUNCHER_ICON ${LAUNCHER_LIB}/launchericon.icns)
    list(APPEND LAUNCHER_SOURCES ${LAUNCHER_ICON})
    set_source_files_properties(${LAUNCHER_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()
if(WIN32)
    enable_language(RC)
    set(GSPLUS_ICO_PATH "${LAUNCHER_LIB}/launchericon.ico")
    configure_file("${EB_SRC}/emubuddy.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" @ONLY)
    list(APPEND LAUNCHER_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc")
endif()

add_executable(emubuddy-launcher ${LAUNCHER_SOURCES})
target_include_directories(emubuddy-launcher PRIVATE src ${EB_UI})
target_compile_definitions(emubuddy-launcher PRIVATE
    UI_THEMABLE=1
    LAUNCHER_VERSION_STR="${EMUBUDDY_VERSION}")
target_link_libraries(emubuddy-launcher PRIVATE launcher_profile launcher_library SDL3::SDL3)

if(APPLE)
    set_target_properties(emubuddy-launcher PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "EmuBuddy Launcher"
        MACOSX_BUNDLE_BUNDLE_NAME "EmuBuddy Launcher"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.portwally.EmuBuddyLauncher"
        MACOSX_BUNDLE_BUNDLE_VERSION "${EMUBUDDY_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${EMUBUDDY_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "launchericon.icns"
        INSTALL_RPATH "@executable_path/../Frameworks")
    # Re-sign after the icon went into the bundle (see EmuBuddy's CMakeLists).
    add_custom_command(TARGET emubuddy-launcher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:emubuddy-launcher>/EmuBuddy Launcher.d"
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:emubuddy-launcher>"
        COMMENT "Re-signing EmuBuddy Launcher.app (ad-hoc)"
        VERBATIM)
    install(TARGETS emubuddy-launcher BUNDLE DESTINATION .)
    install(CODE "
        include(BundleUtilities)
        fixup_bundle(\"\${CMAKE_INSTALL_PREFIX}/EmuBuddy Launcher.app\" \"\" \"$<TARGET_FILE_DIR:SDL3::SDL3>\")
        execute_process(COMMAND codesign --force --deep --sign -
            \"\${CMAKE_INSTALL_PREFIX}/EmuBuddy Launcher.app\")
    ")
else()
    set_target_properties(emubuddy-launcher PROPERTIES
        OUTPUT_NAME "EmuBuddy-Launcher"
        INSTALL_RPATH "$ORIGIN")
    install(TARGETS emubuddy-launcher RUNTIME DESTINATION .)
    if(WIN32)
        add_custom_command(TARGET emubuddy-launcher POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:emubuddy-launcher> $<TARGET_FILE_DIR:emubuddy-launcher>
            COMMAND_EXPAND_LISTS)
        install(FILES $<TARGET_RUNTIME_DLLS:emubuddy-launcher> DESTINATION .)
    endif()
endif()
