# ============================================================================
#  GSplus - CMake build  (Phase 1, milestone 1: the platform-independent core)
# ============================================================================
#
#  HOW TO READ THIS FILE (CMake in plain terms)
#  --------------------------------------------
#  This file is the cross-platform replacement for the old `Makefile` + `vars`
#  symlink dance. It DESCRIBES the project; CMake then GENERATES the real build
#  for whatever machine you're on. You drive it with two commands:
#
#      cmake -B build          # read this file, set up a ./build folder
#      cmake --build build     # actually compile
#
#  Right now this file only does ONE small thing on purpose: it compiles every
#  platform-independent C file (the emulator core that the old `ldvars` listed)
#  into a static library called `gsplus_core`. A static library is just a bag
#  of compiled .o files (libgsplus_core.a) with no main() and no GUI yet.
#  We build the core ALONE first so that if something breaks, we know it's a
#  plain C compile problem -- not Swift, not linking, not app bundling.
#  Later milestones will add the macOS app and the SDL3 build on top of this.
# ----------------------------------------------------------------------------

# Minimum CMake version. 3.20 is modern, widely available, and is what we'll
# need for the SDL3 + macOS bundle work coming later.
cmake_minimum_required(VERSION 3.20)

# ----------------------------------------------------------------------------
# macOS minimum deployment target. MUST be set BEFORE project()/enable_language
# so the compiler stamps the binary's minimum-OS load command (LC_BUILD_VERSION
# minos) and CMake fills the generated bundle Info.plist's LSMinimumSystemVersion
# with this value. If left unset, the toolchain defaults to the BUILD MACHINE's
# SDK version -- on the macos-latest CI runner that is a newer macOS than many
# users run, so dyld rejects the downloaded app with "You can't use this version
# of the application with this version of macOS". 11.0 is a safe, modern floor
# (first arm64 release) that still runs on older Intel/Apple-Silicon Macs.
# Only meaningful on Apple; harmless elsewhere.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
        "Minimum macOS version the built app will run on")
endif()

# Declare the project: a name, a version, and which languages are used.
# (We'll add Swift here in the next milestone; C is all the core needs.)
project(gsplus
    VERSION 1.1.0
    DESCRIPTION "GSplus - cross-platform Apple IIgs emulator (KEGS-based)"
    LANGUAGES C)

# ----------------------------------------------------------------------------
# Version. EmuBuddy has its own version numbers, starting at 1.0.0. (It is built
# on GSplus 1.38.0, which is KEGS 1.38; those numbers belong to them.) The
# variable is still called GSPLUS_VERSION, as in the code it came from.
# CI passes the exact git tag via -DGSPLUS_VERSION (e.g. -DGSPLUS_VERSION=1.0.0)
# so the tag, the app, and the release artifacts all carry the same version.
# Local builds without -DGSPLUS_VERSION fall back to the project() version above.
# ----------------------------------------------------------------------------
if(NOT DEFINED GSPLUS_VERSION OR GSPLUS_VERSION STREQUAL "")
    set(GSPLUS_VERSION "${PROJECT_VERSION}")
endif()
message(STATUS "GSplus: version ${GSPLUS_VERSION}")

# Make the version string available to all targets defined below (the app's
# window title/startup log in sdl_driver.c and the status line in sim65816.c).
# Scope to C only: Swift rejects valued -D defines (it has present/absent flags).
add_compile_definitions(
    "$<$<COMPILE_LANGUAGE:C>:GSPLUS_VERSION_STR=\"${GSPLUS_VERSION}\">")

# ----------------------------------------------------------------------------
# The platform-independent core sources.
# This is exactly the OBJECTS list from the old `ldvars` file -- the files that
# every platform compiles regardless of OS. (The scc_*driver.c files are here
# because they are guarded by #ifdefs internally and built on all platforms,
# just as the current Makefile does.) We list them explicitly rather than
# globbing, so CMake never silently picks up or drops a file behind our back.
# ----------------------------------------------------------------------------
set(GSPLUS_CORE_SOURCES
    adb.c
    engine_c.c
    clock.c
    config.c
    debugger.c
    scc.c
    localtalk.c
    scc_socket_driver.c
    scc_windriver.c
    scc_unixdriver.c
    iwm.c
    joystick_driver.c
    moremem.c
    paddles.c
    mockingboard.c
    sim65816.c
    smartport.c
    doc.c
    sound.c
    sound_driver.c
    woz.c
    unshk.c
    undeflate.c
    dynapro.c
    dyna_type.c
    dyna_filt.c
    dyna_validate.c
    applesingle.c
    video.c
    voc.c
    compile_time.c
)

# Create the static library target from those sources.
add_library(gsplus_core STATIC ${GSPLUS_CORE_SOURCES})

# Header search path: the sources #include headers sitting next to them, so
# add this directory. PUBLIC means "anything that links gsplus_core also gets
# this include path" -- handy once the app/SDL targets link against the core.
target_include_directories(gsplus_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Compile flags. The old macOS `vars` used: -Wall -O2 -DMAC
#   * -DMAC is a compile DEFINITION that selects macOS code paths throughout
#     the codebase, so we express it with target_compile_definitions.
#   * -O2 comes from the build type (see below); -Wall is a warning flag.
# NOTE: hard-coding MAC here is temporary. In a later milestone this becomes
# per-target/per-platform so the same file builds on Linux and Windows too.
target_compile_definitions(gsplus_core PUBLIC MAC)
target_compile_options(gsplus_core PRIVATE -Wall)

# If the user didn't pick a build type, default to an optimized one (-O2-ish).
# CMake maps "Release" to -O2/-O3 automatically, so we don't hard-code -O2.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# A friendly note printed during configuration so you can see what was chosen.
message(STATUS "GSplus: configured core library (build type: ${CMAKE_BUILD_TYPE})")


# ============================================================================
#  Phase 2: the SDL3 cross-platform target (gsplus-sdl)
# ============================================================================
#
#  This is the portable build: SDL3 provides display, input, and (soon) audio
#  on macOS, Linux, and Windows from one set of driver files (sdl_driver.c,
#  sdl_snd_driver.c). It runs ALONGSIDE the native macOS app above.
#
#  find_package(SDL3) locates SDL3 wherever it's installed (Homebrew, apt, etc.)
#  and gives us the SDL3::SDL3 target carrying its include paths and link flags
#  -- no hand-written -I/-l bookkeeping. This is the payoff we moved to CMake for.
# ----------------------------------------------------------------------------
# Find SDL3 on the system (Homebrew, apt, vcpkg, ...). If it isn't installed --
# common on CI runners, since SDL3 is new -- fetch and build it from source so
# the build is self-sufficient everywhere. Locally this is instant (Homebrew's
# SDL3 is found); only CI without SDL3 pays the one-time build cost.
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
    message(STATUS "GSplus: SDL3 not found on system -- fetching from source")
    include(FetchContent)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.10        # matches the version developed against
        GIT_SHALLOW TRUE)
    set(SDL_SHARED ON  CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST   OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
else()
    message(STATUS "GSplus: using system SDL3 ${SDL3_VERSION}")
endif()

# A SECOND copy of the emulator core, compiled differently from the native one:
# NO -DMAC (so it behaves as a generic build), plus -DSDL_AUDIO so sound_driver.c
# routes audio through the SDL backend instead of CoreAudio. We can't reuse the
# gsplus_core library because its .o files already have -DMAC baked in.
add_library(gsplus_core_sdl STATIC ${GSPLUS_CORE_SOURCES})
target_include_directories(gsplus_core_sdl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# SDL_AUDIO routes sound through the SDL backend; SDL_INPUT compiles the native
# joystick backends (IOKit/joydev/mmsystem) out of joystick_driver.c so the
# SDL_Gamepad backend in sdl_driver.c provides joystick_init/update instead.
target_compile_definitions(gsplus_core_sdl PUBLIC SDL_AUDIO SDL_INPUT)
target_compile_options(gsplus_core_sdl PRIVATE -Wall)

# The portable executable: SDL driver + SDL audio backend, linked against the
# SDL-flavoured core and SDL3 itself. (PUBLIC SDL_AUDIO from the core propagates
# to these sources too, keeping the audio routing consistent.)
# Networking for the Uthernet II card: libslirp (user-mode NAT) when available,
# otherwise a stub backend so the build still works (card present, no network).
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(SLIRP IMPORTED_TARGET slirp)
endif()
if(SLIRP_FOUND)
    set(EMUBUDDY_NET_SOURCES uthernet2.c net_slirp.c)
    message(STATUS "EmuBuddy: networking via libslirp ${SLIRP_VERSION}")
else()
    set(EMUBUDDY_NET_SOURCES uthernet2.c net_none.c)
    message(STATUS "EmuBuddy: libslirp not found - Uthernet II without network")
endif()

# Register-level test for the Uthernet II card + network (not built by default):
#   cmake --build build --target test_uthernet2 && build/test_uthernet2
add_executable(test_uthernet2 EXCLUDE_FROM_ALL tests/test_uthernet2.c
    ${EMUBUDDY_NET_SOURCES})
if(SLIRP_FOUND)
    target_link_libraries(test_uthernet2 PRIVATE PkgConfig::SLIRP)
endif()

# Host test for AppleTalk (localtalk.c), not built by default:
#   cmake --build build --target test_localtalk && build/test_localtalk
add_executable(test_localtalk EXCLUDE_FROM_ALL tests/test_localtalk.c localtalk.c)

# Host test for the bridge (bridge.c), not built by default:
#   cmake --build build --target test_bridge && build/test_bridge
add_executable(test_bridge EXCLUDE_FROM_ALL tests/test_bridge.c bridge.c)
target_link_libraries(test_bridge PRIVATE SDL3::SDL3)

# Test tool for the ProDOS image writer (not built by default):
add_executable(test_prodos_write EXCLUDE_FROM_ALL tests/test_prodos_write.c prodos_img.c)

# Shared folder name test (text files are NAME.txt on the host):
#   cmake --build build --target test_dyna_names && build/test_dyna_names
add_executable(test_dyna_names EXCLUDE_FROM_ALL tests/test_dyna_names.c)

# Virtual ImageWriter II (C++, ported from GSport/GSplus legacy, GPL v2+)
enable_language(CXX)
set(EMUBUDDY_PRINTER_SOURCES imagewriter.cpp iw_font.c printer_scc.c printer_ps.c tool_find.c video_rec.c finder_probe.c ui_printer.c)
set_source_files_properties(imagewriter.cpp PROPERTIES COMPILE_OPTIONS "-w;-std=c++11")

add_executable(gsplus-sdl
    sdl_driver.c
    sdl_snd_driver.c
    png_write.c
    ui.c
    ui_font.c
    ui_font_data.c
    ui_app.c
    ui_drives.c
    ui_slots.c
    ui_controllers.c
    ui_browser.c
    cli.c
    prodos_img.c
    bridge.c
    webgate.c
    ${EMUBUDDY_NET_SOURCES}
    ${EMUBUDDY_PRINTER_SOURCES}
)
target_link_libraries(gsplus-sdl PRIVATE gsplus_core_sdl SDL3::SDL3)
if(SLIRP_FOUND)
    target_link_libraries(gsplus-sdl PRIVATE PkgConfig::SLIRP)
endif()

if(UNIX AND NOT APPLE)
    # paddles.c uses sqrt(); glibc keeps it in libm
    target_link_libraries(gsplus-sdl PRIVATE m)
endif()

if(APPLE)
    # macOS-only: retune SDL's default menu bar so ⌘ combos pass through to the
    # emulated IIgs and GSplus quits on ⌥⌘Q. The Objective-C source needs Cocoa.
    # Explicitly enable the OBJC language so CMake has a proper compile rule for
    # the .m file. A single-arch build tolerates compiling it via the C rule, but
    # a universal build (CMAKE_OSX_ARCHITECTURES=arm64;x86_64) errors with
    # "CMAKE_OBJC_COMPILE_OBJECT not set" unless OBJC is enabled.
    enable_language(OBJC)
    target_sources(gsplus-sdl PRIVATE sdl_mac_menu.m ui_mac_drag.m ui_mac_menu.m)
    target_link_libraries(gsplus-sdl PRIVATE "-framework Cocoa")
    # A plain USB joystick, read from IOKit (SDL never offers it as a gamepad)
    target_sources(gsplus-sdl PRIVATE sdl_mac_hidstick.c)
    target_link_libraries(gsplus-sdl PRIVATE "-framework IOKit")
endif()

if(WIN32)
    # The core uses Winsock (scc serial-over-TCP) and Win32 multimedia timers,
    # which the native Win build links explicitly. SDL3 links its own deps.
    target_link_libraries(gsplus-sdl PRIVATE ws2_32 winmm)

    # Embed the GSplus icon into the .exe (Explorer / taskbar) via a resource
    # script. Bake the absolute .ico path into the .rc so windres always finds it.
    enable_language(RC)
    set(GSPLUS_ICO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../lib/emubuddyicon.ico")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/emubuddy.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/emubuddy.rc" @ONLY)
    target_sources(gsplus-sdl PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/emubuddy.rc")
endif()

# On macOS, build gsplus-sdl as a proper .app bundle with an icon. On Linux and
# Windows it stays a plain executable (MACOSX_* settings are Apple-only). We
# guard with if(APPLE) so the .icns file isn't fed to the compiler elsewhere.
if(APPLE)
    set(GSPLUS_SDL_ICON ${CMAKE_CURRENT_SOURCE_DIR}/../lib/emubuddyicon.icns)
    target_sources(gsplus-sdl PRIVATE ${GSPLUS_SDL_ICON})
    # Tell CMake to drop the icon into the bundle's Contents/Resources.
    set_source_files_properties(${GSPLUS_SDL_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    # The app is "EmuBuddy" -> EmuBuddy.app.
    set_target_properties(gsplus-sdl PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "EmuBuddy"
        MACOSX_BUNDLE_BUNDLE_NAME "EmuBuddy"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.portwally.EmuBuddy"
        MACOSX_BUNDLE_BUNDLE_VERSION "${GSPLUS_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${GSPLUS_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "emubuddyicon.icns"
        # Look for bundled dylibs next to the executable in ../Frameworks.
        INSTALL_RPATH "@executable_path/../Frameworks")

    # Adding the icon to Resources invalidates the linker's ad-hoc code signature
    # ("code has no resources but signature indicates they must be present"),
    # which makes Finder reject the bundle and show the prohibitory icon. Re-sign
    # ad-hoc after the build so the bundle is valid and shows its icon. First
    # delete the Ninja depfile (EmuBuddy.d) that lands next to the executable --
    # codesign chokes on that stray file in Contents/MacOS.
    # AppleTalk (localtalk.c) talks to other programs and computers in the local
    # network by UDP multicast. macOS asks the user before it lets an app do
    # that; this is the text of the question. (plutil -replace adds or replaces.)
    # Who signs the built apps: "-" (ad-hoc, the default, and what the release
    # packages use) or a certificate such as "Apple Development". An ad-hoc app
    # is known to macOS only by its exact contents, so every rebuild is a new
    # app and the privacy questions (the Documents folder, the local network)
    # come again; signed with a certificate it stays the same app.
    #   cmake -DEMUBUDDY_SIGN_IDENTITY="Apple Development" build
    set(EMUBUDDY_SIGN_IDENTITY "-" CACHE STRING "codesign identity for the built apps (- = ad-hoc)")
    add_custom_command(TARGET gsplus-sdl POST_BUILD
        COMMAND plutil -replace NSLocalNetworkUsageDescription -string
            "EmuBuddy's Apple IIgs uses the local network for AppleTalk: to find file servers, printers and other computers."
            "$<TARGET_BUNDLE_DIR:gsplus-sdl>/Contents/Info.plist"
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:gsplus-sdl>/EmuBuddy.d"
        COMMAND codesign --force --sign "${EMUBUDDY_SIGN_IDENTITY}" "$<TARGET_BUNDLE_DIR:gsplus-sdl>"
        COMMENT "Re-signing EmuBuddy.app (${EMUBUDDY_SIGN_IDENTITY})"
        VERBATIM)
endif()

if(NOT APPLE)
    # The binary is just "EmuBuddy" (no -sdl). $ORIGIN lets it find a co-located
    # SDL3 shared library at runtime (ELF).
    set_target_properties(gsplus-sdl PROPERTIES
        OUTPUT_NAME "EmuBuddy"
        INSTALL_RPATH "$ORIGIN")
endif()

if(WIN32)
    # Copy SDL3.dll next to the .exe so it runs from the build tree and gets
    # packaged alongside the executable.
    add_custom_command(TARGET gsplus-sdl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:gsplus-sdl> $<TARGET_FILE_DIR:gsplus-sdl>
        COMMAND_EXPAND_LISTS)
endif()

message(STATUS "GSplus: configured SDL3 target (gsplus-sdl)")


# ============================================================================
#  Phase 3: packaging ("installation a breeze")
# ============================================================================
#
#  `cmake --install build --prefix <dir>` stages the app; `cpack` wraps the
#  staged files into a downloadable installer. The goal is artifacts a user can
#  download and run with NO prerequisites (no Homebrew, no SDL install).
# ----------------------------------------------------------------------------
if(APPLE)
    # Install the .app, then make it SELF-CONTAINED. fixup_bundle scans the
    # binary's dependencies, copies the non-system ones (libSDL3) into
    # Contents/Frameworks, and rewrites the recorded paths to point inside the
    # bundle. We then re-sign ad-hoc ("-"): editing a binary invalidates its
    # code signature, and arm64 macOS refuses to launch a broken-signature app.
    install(TARGETS gsplus-sdl BUNDLE DESTINATION .)
    # Tell fixup_bundle which directory to search for @rpath-named dylibs. A
    # Homebrew SDL3 records an absolute/resolvable install name, but a
    # FetchContent-built SDL3 (the universal job, and any host without system
    # SDL3) names itself @rpath/libSDL3.0.dylib -- fixup_bundle can't find that
    # unless we hand it the directory containing the built dylib. The genex
    # resolves to Homebrew's libdir or the FetchContent build dir as appropriate.
    install(CODE "
        include(BundleUtilities)
        fixup_bundle(\"\${CMAKE_INSTALL_PREFIX}/EmuBuddy.app\" \"\" \"$<TARGET_FILE_DIR:SDL3::SDL3>\")
        execute_process(COMMAND codesign --force --deep --sign -
            \"\${CMAKE_INSTALL_PREFIX}/EmuBuddy.app\")
    ")
    set(CPACK_GENERATOR "DragNDrop")          # -> a .dmg
    set(CPACK_DMG_VOLUME_NAME "EmuBuddy")
    # Say in the file name which Macs it is for (EmuBuddy-x.y.z-macOS-arm64.dmg,
    # not "-Darwin"): the app only runs on the processor it was built for.
    set(CPACK_SYSTEM_NAME "macOS-${CMAKE_SYSTEM_PROCESSOR}")
else()
    # Install to the package root (no bin/ subdir) so the .zip/.tar.gz has the
    # executable at the top level.
    install(TARGETS gsplus-sdl RUNTIME DESTINATION .)
    if(WIN32)
        # Bundle SDL3.dll next to the .exe so the ZIP is self-contained.
        install(FILES $<TARGET_RUNTIME_DLLS:gsplus-sdl> DESTINATION .)
        set(CPACK_GENERATOR "ZIP")            # CI can upgrade to an NSIS installer
    else()
        # On Linux the SDL3 .so is bundled by the AppImage step in CI; a plain
        # TGZ relies on the system SDL3 -- README-linux.txt tells the user how
        # to install it (distro package or build from source).
        install(FILES packaging/README-linux.txt DESTINATION .)
        set(CPACK_GENERATOR "TGZ")
        # Put the CPU architecture in the archive name (GSplus-x.y.z-Linux-x86_64
        # vs -Linux-aarch64) so the two Linux release assets don't collide.
        set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    # Libraries the CI collects in build/bundle go into the download, next to the
    # programs, so it runs without installing anything: on Windows the DLLs
    # besides SDL3 (the MinGW runtime, libslirp and what it needs), on Linux
    # SDL3 and libslirp. A local build has no such folder; then nothing is added.
    install(DIRECTORY "${CMAKE_BINARY_DIR}/bundle/" DESTINATION . OPTIONAL)
    # No top-level "<name>-<version>" wrapper dir -- put files at the archive root.
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
endif()

# The Launcher (profiles and skins) ships in the same download. Switch it off
# with -DEMUBUDDY_WITH_LAUNCHER=OFF.
option(EMUBUDDY_WITH_LAUNCHER "Build and package EmuBuddy Launcher too" ON)
if(EMUBUDDY_WITH_LAUNCHER AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../launcher/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../launcher" launcher)
endif()

# Every artifact ships the project README and the GPL license text (the GPL
# requires the license to accompany distributed binaries). These paths are
# relative to emubuddy/src, so ../.. is the repository root.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../README.md"
              "${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSE"
        DESTINATION .)

set(CPACK_PACKAGE_NAME "EmuBuddy")
set(CPACK_PACKAGE_VENDOR "EmuBuddy")
set(CPACK_PACKAGE_VERSION "${GSPLUS_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
include(CPack)
