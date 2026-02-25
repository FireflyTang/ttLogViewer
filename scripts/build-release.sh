#!/usr/bin/env bash
# build-release.sh — Build a static, single-file ttLogViewer binary for the current platform.
#
# Usage:
#   bash scripts/build-release.sh
#
# Output:
#   dist/ttLogViewer-v{VERSION}-{platform}[.exe]
#
# Requirements:
#   - CMake 3.21+, Ninja
#   - Platform-appropriate C++23 compiler
#   - On Windows: MSYS2 MinGW64 (default paths assumed)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

# ── Read version from CMakeLists.txt ───────────────────────────────────────────
VERSION=$(grep -oP '(?<=VERSION )[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | head -1)
if [[ -z "${VERSION}" ]]; then
    echo "ERROR: Could not read VERSION from CMakeLists.txt" >&2
    exit 1
fi
echo "Building ttLogViewer v${VERSION} ..."

# ── Platform detection ─────────────────────────────────────────────────────────
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        PLATFORM="windows-x64"
        CMAKE_C_COMPILER="C:/msys64/mingw64/bin/gcc.exe"
        CMAKE_CXX_COMPILER="C:/msys64/mingw64/bin/g++.exe"
        NINJA="/c/msys64/mingw64/bin/ninja.exe"
        BINARY="ttLogViewer.exe"
        ;;
    Linux*)
        PLATFORM="linux-x64"
        CMAKE_C_COMPILER="${CC:-gcc}"
        CMAKE_CXX_COMPILER="${CXX:-g++}"
        NINJA="${NINJA:-ninja}"
        BINARY="ttLogViewer"
        ;;
    Darwin*)
        PLATFORM="macos-arm64"
        CMAKE_C_COMPILER="${CC:-$(xcrun -find clang 2>/dev/null || echo clang)}"
        CMAKE_CXX_COMPILER="${CXX:-$(xcrun -find clang++ 2>/dev/null || echo clang++)}"
        NINJA="${NINJA:-ninja}"
        BINARY="ttLogViewer"
        ;;
    *)
        echo "ERROR: Unknown platform '$(uname -s)'" >&2
        exit 1
        ;;
esac

BUILD_DIR="build-release"

# ── Configure ──────────────────────────────────────────────────────────────────
cmake -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="${CMAKE_C_COMPILER}" \
    -DCMAKE_CXX_COMPILER="${CMAKE_CXX_COMPILER}" \
    -DSTATIC_RUNTIME=ON \
    -DBUILD_TESTING=OFF

# ── Build ──────────────────────────────────────────────────────────────────────
"${NINJA}" -C "${BUILD_DIR}" ttLogViewer

# ── Package ────────────────────────────────────────────────────────────────────
mkdir -p dist

if [[ "${BINARY}" == *.exe ]]; then
    OUT="dist/ttLogViewer-v${VERSION}-${PLATFORM}.exe"
else
    OUT="dist/ttLogViewer-v${VERSION}-${PLATFORM}"
fi

cp "${BUILD_DIR}/bin/${BINARY}" "${OUT}"
echo ""
echo "Packaged → ${OUT}"

# ── Verify (ldd / otool) ───────────────────────────────────────────────────────
echo ""
echo "Dependency check:"
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        ldd "${OUT}" | grep -v "WINDOWS\|ntdll\|KERNEL\|kernel\|VCRUNTIME\|vcruntime\|api-ms" || true
        ;;
    Linux*)
        ldd "${OUT}" || true
        ;;
    Darwin*)
        otool -L "${OUT}" || true
        ;;
esac

echo ""
echo "Done. Binary size: $(du -sh "${OUT}" | cut -f1)"
