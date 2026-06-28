#!/usr/bin/env bash
#
# build-hamlib-android.sh — cross-compile libhamlib for Android (arm64) with
# the hamlib_ex USB-serial bridge applied.
#
# Produces an install tree (headers + libs) suitable for feeding to the
# hamlib_nif crate's build.rs via:
#
#     export HAMLIB_INCLUDE_DIR=<destdir>/usr/local/include
#     export HAMLIB_LIB_DIR=<destdir>/usr/local/lib
#
# Mirrors the JS8Call-improved recipe (NDK LLVM toolchain, API 28,
# --without-libusb), plus our bridge: the android_serial_bridge.{c,h} sources
# are copied into src/ and the 0001 patch wires them into serial.c / misc.c /
# rig.c / Makefile.am.
#
# Requirements: Android NDK (r28+), autotools (autoconf/automake/libtool),
# a git checkout of Hamlib at the pinned tag, and the usual build tools.
#
# Usage:
#   ANDROID_NDK_HOME=~/Android/Sdk/ndk/28.0.12433566 \
#     ./build-hamlib-android.sh [aarch64|armv7a|x86_64]
#
# Default ABI is aarch64 (the phone). Output lands in ./out/<abi>.

set -euo pipefail

# ── Configuration ───────────────────────────────────────────────────────────

HAMLIB_TAG="${HAMLIB_TAG:-4.6.5}"
HAMLIB_REPO="${HAMLIB_REPO:-https://github.com/Hamlib/Hamlib.git}"
API="${API:-28}"
ABI="${1:-aarch64}"

# Map ABI -> autotools host triple.
case "$ABI" in
  aarch64) HOST_TRIPLE="aarch64-linux-android" ;;
  armv7a)  HOST_TRIPLE="armv7a-linux-androideabi" ;;
  x86_64)  HOST_TRIPLE="x86_64-linux-android" ;;
  *) echo "unknown ABI '$ABI' (want aarch64|armv7a|x86_64)" >&2; exit 2 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BRIDGE_SRC_DIR="$SCRIPT_DIR/src"
PATCH="$SCRIPT_DIR/patches/0001-android-usb-serial-bridge.patch"

WORK="$SCRIPT_DIR/work/$ABI"
SRC="$WORK/Hamlib"
DEST="$SCRIPT_DIR/out/$ABI"

# ── Locate the NDK toolchain ────────────────────────────────────────────────

NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:-${NDK:-}}}"
if [[ -z "${NDK}" || ! -d "${NDK}" ]]; then
  echo "Set ANDROID_NDK_HOME to your NDK install (e.g. ~/Android/Sdk/ndk/28.x)." >&2
  exit 1
fi

# Host tag for the prebuilt toolchain dir.
case "$(uname -s)" in
  Darwin) NDK_HOST_TAG="darwin-x86_64" ;;
  Linux)  NDK_HOST_TAG="linux-x86_64" ;;
  *) echo "unsupported build host $(uname -s)" >&2; exit 1 ;;
esac

TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/$NDK_HOST_TAG"
if [[ ! -d "$TOOLCHAIN" ]]; then
  echo "NDK toolchain not found at $TOOLCHAIN" >&2
  exit 1
fi

export AR="$TOOLCHAIN/bin/llvm-ar"
export AS="$TOOLCHAIN/bin/llvm-as"
export CC="$TOOLCHAIN/bin/${HOST_TRIPLE}${API}-clang"
export CXX="$TOOLCHAIN/bin/${HOST_TRIPLE}${API}-clang++"
export LD="$TOOLCHAIN/bin/ld"
export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
export STRIP="$TOOLCHAIN/bin/llvm-strip"

for tool in "$CC" "$AR" "$RANLIB" "$STRIP"; do
  [[ -x "$tool" ]] || { echo "missing toolchain binary: $tool" >&2; exit 1; }
done

echo "==> NDK:        $NDK"
echo "==> toolchain:  $TOOLCHAIN"
echo "==> ABI/host:   $ABI / $HOST_TRIPLE (API $API)"
echo "==> Hamlib tag: $HAMLIB_TAG"
echo "==> output:     $DEST"

# ── Fetch + pin Hamlib source ───────────────────────────────────────────────

mkdir -p "$WORK"
if [[ ! -d "$SRC/.git" ]]; then
  echo "==> cloning Hamlib $HAMLIB_TAG"
  git clone --depth 1 --branch "$HAMLIB_TAG" "$HAMLIB_REPO" "$SRC"
else
  echo "==> reusing existing checkout, resetting to $HAMLIB_TAG"
  git -C "$SRC" fetch --depth 1 origin "tag" "$HAMLIB_TAG" || true
  git -C "$SRC" checkout -f "$HAMLIB_TAG"
  git -C "$SRC" reset --hard "$HAMLIB_TAG"
  git -C "$SRC" clean -fdx
fi

# ── Copy bridge sources + apply the hook patch ──────────────────────────────

echo "==> copying bridge sources into src/"
cp "$BRIDGE_SRC_DIR/android_serial_bridge.c" "$SRC/src/"
cp "$BRIDGE_SRC_DIR/android_serial_bridge.h" "$SRC/src/"

echo "==> applying $PATCH"
git -C "$SRC" apply "$PATCH"

# ── Configure + build ───────────────────────────────────────────────────────

cd "$SRC"

echo "==> bootstrap"
./bootstrap

echo "==> configure"
# --without-libusb is mandatory: no libusb cross build, USB goes through the
# bridge. Static lib only (no shared) keeps deployment simple for the NIF link;
# flip to --enable-shared if a .so is preferred.
./configure \
  --host="$HOST_TRIPLE" \
  --without-libusb \
  --disable-shared \
  --enable-static \
  --without-cxx-binding \
  CFLAGS="-fPIC -O2"

echo "==> make"
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" V=0

echo "==> install to $DEST"
rm -rf "$DEST"
mkdir -p "$DEST"
make install DESTDIR="$DEST"

# ── Report the paths build.rs needs ─────────────────────────────────────────

INC="$DEST/usr/local/include"
LIB="$DEST/usr/local/lib"

echo
echo "================================================================"
echo "libhamlib ($HAMLIB_TAG, $ABI) built with USB-serial bridge."
echo
echo "Point the hamlib_nif crate at it:"
echo "  export HAMLIB_INCLUDE_DIR=$INC"
echo "  export HAMLIB_LIB_DIR=$LIB"
echo
echo "Artifacts:"
ls -1 "$LIB"/libhamlib.* 2>/dev/null || echo "  (no libhamlib.* found — check configure output)"
echo "================================================================"
