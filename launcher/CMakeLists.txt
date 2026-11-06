# EmuBuddy Launcher: a front end for EmuBuddy with profiles and skins.
# Plan: ../docs/LAUNCHER_PLAN.md
cmake_minimum_required(VERSION 3.20)
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()
project(emubuddy-launcher VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
if(NOT DEFINED EMUBUDDY_VERSION OR EMUBUDDY_VERSION STREQUAL "")
    set(EMUBUDDY_VERSION "${PROJECT_VERSION}")
endif()

# profile.c has no SDL; it is tested on its own.
add_library(launcher_profile STATIC src/profile.c)
add_executable(test_profile tests/test_profile.c)
target_link_libraries(test_profile PRIVATE launcher_profile)
enable_testing()
add_test(NAME profile COMMAND test_profile)

find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
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

add_executable(emubuddy-launcher src/main.c)
target_compile_definitions(emubuddy-launcher PRIVATE
    LAUNCHER_VERSION_STR="${EMUBUDDY_VERSION}")
target_link_libraries(emubuddy-launcher PRIVATE launcher_profile SDL3::SDL3)
