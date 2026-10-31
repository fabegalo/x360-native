#!/bin/sh
# One-time setup after cloning on Linux or macOS (bootstrap.ps1 on Windows):
# SDK submodules, the pinned build tools of scripts/packaging/posix-toolchain.lock
# and the local SDK patches of deps.lock.json. No root, nothing installed
# outside this repository. Re-running reuses everything already verified.
#   scripts/bootstrap.sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
Linux-x86_64) platform=linux-x64 preset=linux-amd64 ;;
Linux-aarch64 | Linux-arm64) platform=linux-arm64 preset=linux-arm64 ;;
Darwin-arm64) platform=macos-arm64 preset=mac-arm64 ;;
Darwin-x86_64) platform=macos-x64 preset=mac-amd64 ;;
*) echo "unsupported host: $(uname -s) $(uname -m)" >&2; exit 2 ;;
esac

# Shallow and parallel: only the pinned commits are needed to build.
git -C "$root" submodule update --init --depth 1 external/rexglue-sdk
git -C "$root/external/rexglue-sdk" submodule update --init --recursive --depth 1 --jobs 8

# LLVM (Linux), CMake (macOS) and Ninja, each verified against the lock.
sh "$root/scripts/packaging/prepare-posix-toolchain.sh" "$platform" "$root/.local/toolchains/posix-$platform"

python3 "$root/scripts/setup/apply-sdk-patches.py"

tools="$root/.local/toolchains/posix-$platform"
cmake=cmake
[ -x "$tools/cmake/bin/cmake" ] && cmake="$tools/cmake/bin/cmake"
cat <<EOF

Next (from $root):
  $cmake --preset $preset
  $cmake --build --preset $preset-release
  ctest --preset $preset-release -L project
EOF
