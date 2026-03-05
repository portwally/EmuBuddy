#!/bin/bash
#
# build-emubuddy-mame.sh
# Builds a minimal MAME binary containing only Apple II/IIGS drivers
# and all expansion cards, peripherals, and slot devices.
#
# Usage:
#   ./build-emubuddy-mame.sh              # Build standalone binary
#   ./build-emubuddy-mame.sh --libmame    # Build as shared library (dylib)
#   ./build-emubuddy-mame.sh --clean      # Clean previous build artifacts
#   ./build-emubuddy-mame.sh --help       # Show usage
#

set -e

# ── Configuration ──────────────────────────────────────────────────────────

SUBTARGET="emubuddy"
MAME_DIR="${MAME_DIR:-$(pwd)/mame}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"
BUILD_MODE="binary"  # binary or libmame

# Apple II driver sources (MAME auto-resolves bus devices and dependencies)
# These three files pull in ALL slot devices via cards.cpp includes:
#   - src/devices/bus/a2bus/*        (all expansion cards)
#   - src/devices/bus/a2gameio/*     (joystick, paddles, etc.)
#   - src/devices/bus/a2kbd/*        (keyboards)
#   - src/devices/bus/applepp/*      (parallel printer)
#   - IWM, Disk II, SmartPort, SCSI, etc.
SOURCES="apple/apple2.cpp,apple/apple2e.cpp,apple/apple2gs.cpp"

# ── Color output helpers ───────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

info()    { echo -e "${CYAN}[INFO]${NC} $1"; }
success() { echo -e "${GREEN}[OK]${NC} $1"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $1"; }
error()   { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# ── Parse arguments ────────────────────────────────────────────────────────

for arg in "$@"; do
    case $arg in
        --libmame)
            BUILD_MODE="libmame"
            shift
            ;;
        --clean)
            info "Cleaning build artifacts for subtarget '${SUBTARGET}'..."
            rm -rf "${MAME_DIR}/build/projects/sdl3/${SUBTARGET}"
            rm -rf "${MAME_DIR}/build/projects/sdl/${SUBTARGET}"
            rm -rf "${MAME_DIR}/build/mingw-gcc/${SUBTARGET}"
            rm -rf "${MAME_DIR}/obj/${SUBTARGET}"*
            success "Clean complete."
            exit 0
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --libmame    Build as shared library (libmame.dylib) instead of binary"
            echo "  --clean      Clean previous build artifacts"
            echo "  --help       Show this help"
            echo ""
            echo "Environment variables:"
            echo "  MAME_DIR     Path to MAME source (default: ./mame)"
            echo "  JOBS         Number of parallel jobs (default: auto-detect)"
            exit 0
            ;;
    esac
done

# ── Preflight checks ──────────────────────────────────────────────────────

info "EmuBuddy MAME Builder"
info "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Check for Xcode command line tools
if ! xcode-select -p &>/dev/null; then
    error "Xcode command line tools not found. Install with: xcode-select --install"
fi
success "Xcode command line tools found"

# Check for SDL3 (MAME has moved to SDL3)
if pkg-config --exists sdl3 2>/dev/null; then
    SDL3_VERSION=$(pkg-config --modversion sdl3)
    success "SDL3 ${SDL3_VERSION} found (via pkg-config)"
elif [ -d "/Library/Frameworks/SDL3.framework" ]; then
    success "SDL3.framework found"
elif brew list sdl3 &>/dev/null; then
    success "SDL3 found (via Homebrew)"
else
    error "SDL3 not found. Install with: brew install sdl3"
fi

# Check for Python 3
if command -v python3 &>/dev/null; then
    PYTHON_VERSION=$(python3 --version 2>&1)
    success "${PYTHON_VERSION} found"
else
    error "Python 3 not found. Install with: brew install python3"
fi

# ── Clone or update MAME source ───────────────────────────────────────────

if [ ! -d "${MAME_DIR}" ]; then
    info "MAME source not found at ${MAME_DIR}"
    info "Cloning MAME repository (this will take a while)..."
    git clone --depth 1 https://github.com/mamedev/mame.git "${MAME_DIR}"
    success "MAME source cloned"
else
    success "MAME source found at ${MAME_DIR}"
    info "Updating to latest..."
    cd "${MAME_DIR}" && git pull --ff-only 2>/dev/null || warn "Could not update (not a git repo or no network)"
fi

cd "${MAME_DIR}"

# Show MAME version
if [ -f "src/version.cpp" ]; then
    MAME_VER=$(grep -oP 'BARE_BUILD_VERSION.*?"[^"]*"' src/version.cpp 2>/dev/null | grep -oP '"[^"]*"' || echo "unknown")
    info "MAME version: ${MAME_VER}"
fi

# ── Verify Apple II sources exist ──────────────────────────────────────────

info "Verifying Apple II driver sources..."
for src in apple/apple2.cpp apple/apple2e.cpp apple/apple2gs.cpp; do
    if [ ! -f "src/mame/${src}" ]; then
        error "Missing driver source: src/mame/${src}"
    fi
done
success "All Apple II driver sources present"

# Count expansion card files
A2BUS_COUNT=$(ls src/devices/bus/a2bus/*.cpp 2>/dev/null | wc -l | tr -d ' ')
A2GAMEIO_COUNT=$(ls src/devices/bus/a2gameio/*.cpp 2>/dev/null | wc -l | tr -d ' ')
A2KBD_COUNT=$(ls src/devices/bus/a2kbd/*.cpp 2>/dev/null | wc -l | tr -d ' ')
info "Found ${A2BUS_COUNT} expansion card sources, ${A2GAMEIO_COUNT} game I/O sources, ${A2KBD_COUNT} keyboard sources"

# ── Build ──────────────────────────────────────────────────────────────────

info "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
info "Build mode: ${BUILD_MODE}"
info "Subtarget: ${SUBTARGET}"
info "Sources: ${SOURCES}"
info "Parallel jobs: ${JOBS}"
info "Architecture: $(uname -m)"
info "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

if [ "${BUILD_MODE}" = "libmame" ]; then
    info "Building libmame shared library..."
    make BUILD_LIBMAME=1 \
         SUBTARGET="${SUBTARGET}" \
         SOURCES="${SOURCES}" \
         ARCHOPTS="-I/opt/homebrew/include" \
         LDFLAGS="-L/opt/homebrew/lib -lSDL3" \
         LDFLAGS_EXTRA="-Wl,-current_version,1.0.0 -Wl,-install_name,@rpath/libmame.dylib" \
         USE_LIBSDL=1 \
         REGENIE=1 \
         -j"${JOBS}" \
         libmame

    # Find the output
    DYLIB_PATH=$(find obj/ -name "libmame*.dylib" 2>/dev/null | head -1)
    if [ -n "${DYLIB_PATH}" ]; then
        success "Library built: ${DYLIB_PATH}"
        ls -lh "${DYLIB_PATH}"
    else
        error "Build completed but libmame.dylib not found"
    fi
else
    info "Building standalone binary..."
    make SUBTARGET="${SUBTARGET}" \
         SOURCES="${SOURCES}" \
         ARCHOPTS="-I/opt/homebrew/include" \
         LDFLAGS="-L/opt/homebrew/lib -lSDL3" \
         USE_LIBSDL=1 \
         REGENIE=1 \
         -j"${JOBS}"

    # Output binary is named <subtarget> (not mame<subtarget>)
    BINARY_NAME="${SUBTARGET}"
    if [ -f "${BINARY_NAME}" ]; then
        success "Binary built: ${MAME_DIR}/${BINARY_NAME}"
        ls -lh "${BINARY_NAME}"

        # Show what machines are included
        echo ""
        info "Included machines:"
        ./"${BINARY_NAME}" -listfull 2>/dev/null | head -30 || true
    else
        error "Build completed but ${BINARY_NAME} not found"
    fi
fi

echo ""
info "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
success "Build complete!"

# Show included slot devices
if [ "${BUILD_MODE}" = "binary" ] && [ -f "${SUBTARGET}" ]; then
    echo ""
    info "Included slot devices for apple2gs:"
    ./"${SUBTARGET}" apple2gs -listslots 2>/dev/null || true
fi
