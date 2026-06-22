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

# Declare the project: a name, a version, and which languages are used.
# (We'll add Swift here in the next milestone; C is all the core needs.)
project(gsplus
    VERSION 0.0.1
    DESCRIPTION "GSplus - cross-platform Apple IIgs emulator (KEGS-based)"
    LANGUAGES C)

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
#  Phase 1, milestone 2: the macOS native app (GSplus.app)
# ============================================================================
#
#  Goal: reproduce what the Makefile's `gsplus:` target does -- link the core
#  library together with the Mac sound driver (macsnd_driver.c) and the two
#  Swift/Cocoa files into a runnable GSplus.app. The Swift files own the app's
#  entry point (AppDelegate has `@main` -> NSApplicationMain) and call into the
#  C core through the bridging header (Kegs-Bridging-Header.h imports defc.h).
#
#  KEY IMPROVEMENT over the Makefile: the old `comp_swift` script hard-codes
#  /Applications/Xcode.app paths and the Mac `vars` hard-codes an SDK path that
#  doesn't even exist on a Command-Line-Tools-only machine. Here we let CMake's
#  built-in Swift support discover the toolchain and -- crucially -- handle the
#  Swift runtime linking / rpaths automatically, which is the fragile part the
#  Makefile does by hand.
# ----------------------------------------------------------------------------
if(APPLE)
    # Turn on CMake's Swift language support. This makes CMake able to compile
    # .swift files and, when it links a target that contains Swift, pull in the
    # Swift runtime and set rpaths for us.
    enable_language(Swift)

    # The macOS application. MACOSX_BUNDLE tells CMake to produce a .app bundle
    # (Contents/MacOS, Info.plist, etc.) instead of a bare command-line binary.
    # We feed it the two Swift files plus the Mac-only sound driver; the rest of
    # the emulator comes in via the gsplus_core library we link below.
    add_executable(gsplus MACOSX_BUNDLE
        AppDelegate.swift
        MainView.swift
        macsnd_driver.c
    )

    # Name the produced app "GSplus" -> GSplus.app (matches the Makefile output).
    # Swift_MODULE_NAME mirrors comp_swift's `-module-name Kegs`.
    set_target_properties(gsplus PROPERTIES
        OUTPUT_NAME "GSplus"
        Swift_MODULE_NAME "Kegs"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    )

    # Swift-only compile flags (the genex applies them ONLY to .swift sources,
    # not to macsnd_driver.c). These mirror the old comp_swift invocation:
    #   -import-objc-header  : the bridging header that exposes C (defc.h) to Swift
    #   -swift-version 4     : the language mode the Swift sources were written in
    #   -Onone               : no Swift optimization (same as comp_swift)
    #   -Xcc -I<srcdir>      : let the Swift clang-importer find defc.h & friends
    target_compile_options(gsplus PRIVATE
        "$<$<COMPILE_LANGUAGE:Swift>:-import-objc-header;${CMAKE_CURRENT_SOURCE_DIR}/Kegs-Bridging-Header.h;-swift-version;4;-Onone;-Xcc;-I${CMAKE_CURRENT_SOURCE_DIR}>"
    )

    # Link the emulator core plus the macOS frameworks the code needs:
    #   Cocoa        -> the AppKit GUI used by the Swift files
    #   AudioToolbox / CoreAudio / AudioUnit -> used by macsnd_driver.c
    # (Cocoa/CoreGraphics etc. imported by Swift are autolinked from the Swift
    #  objects, but we name the ones the C driver needs explicitly to be safe.)
    target_link_libraries(gsplus PRIVATE
        gsplus_core
        "-framework Cocoa"
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework AudioUnit"
    )

    message(STATUS "GSplus: configured macOS app target (GSplus.app)")
endif()


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
target_compile_definitions(gsplus_core_sdl PUBLIC SDL_AUDIO)
target_compile_options(gsplus_core_sdl PRIVATE -Wall)

# The portable executable: SDL driver + SDL audio backend, linked against the
# SDL-flavoured core and SDL3 itself. (PUBLIC SDL_AUDIO from the core propagates
# to these sources too, keeping the audio routing consistent.)
add_executable(gsplus-sdl
    sdl_driver.c
    sdl_snd_driver.c
)
target_link_libraries(gsplus-sdl PRIVATE gsplus_core_sdl SDL3::SDL3)

# On macOS, build gsplus-sdl as a proper .app bundle with an icon. On Linux and
# Windows it stays a plain executable (MACOSX_* settings are Apple-only). We
# guard with if(APPLE) so the .icns file isn't fed to the compiler elsewhere.
if(APPLE)
    set(GSPLUS_SDL_ICON ${CMAKE_CURRENT_SOURCE_DIR}/../lib/kegsicon.icns)
    target_sources(gsplus-sdl PRIVATE ${GSPLUS_SDL_ICON})
    # Tell CMake to drop the icon into the bundle's Contents/Resources.
    set_source_files_properties(${GSPLUS_SDL_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(gsplus-sdl PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "GSplus-SDL"
        MACOSX_BUNDLE_BUNDLE_NAME "GSplus-SDL"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.gsplus.sdl"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "kegsicon.icns"
        # Look for bundled dylibs next to the executable in ../Frameworks.
        INSTALL_RPATH "@executable_path/../Frameworks")
endif()

if(NOT APPLE)
    # Linux/Windows: find a co-located SDL3 shared library next to the binary
    # ($ORIGIN is the runtime path of the executable on ELF systems).
    set_target_properties(gsplus-sdl PROPERTIES INSTALL_RPATH "$ORIGIN")
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
    install(CODE [[
        include(BundleUtilities)
        fixup_bundle("${CMAKE_INSTALL_PREFIX}/GSplus-SDL.app" "" "")
        execute_process(COMMAND codesign --force --deep --sign -
            "${CMAKE_INSTALL_PREFIX}/GSplus-SDL.app")
    ]])
    set(CPACK_GENERATOR "DragNDrop")          # -> a .dmg
    set(CPACK_DMG_VOLUME_NAME "GSplus")
else()
    install(TARGETS gsplus-sdl RUNTIME DESTINATION bin)
    if(WIN32)
        # Bundle SDL3.dll next to the .exe so the ZIP is self-contained.
        install(FILES $<TARGET_RUNTIME_DLLS:gsplus-sdl> DESTINATION bin)
        set(CPACK_GENERATOR "ZIP")            # CI can upgrade to an NSIS installer
    else()
        # On Linux the SDL3 .so is bundled by the AppImage step in CI; a plain
        # TGZ relies on the system SDL3.
        set(CPACK_GENERATOR "TGZ")
    endif()
endif()

set(CPACK_PACKAGE_NAME "GSplus")
set(CPACK_PACKAGE_VENDOR "GSplus Contributors")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
include(CPack)
