#!/bin/sh
# Linux x64 / macOS ARM64 distribution build for package-desktop.ps1.
# Builds the exported, patched source with the pinned tools of
# prepare-posix-toolchain.sh, runs only the distribution tests and writes the
# binaries plus raw compiler/linker evidence to <evidence>. Never installs,
# publishes or touches the user's data.
#   build <platform> <exported-source> <work> <evidence> <version> <payload-sha256> <tools>
#   archive <package-dir> <work> <output.tar.gz> <executable>...
#   clean <work>
# <work> holds source/ and build/ as siblings: the distribution compiler launcher
# passes the source path relative to the build directory. When the export is not
# <work>/source (WSL from drvfs), it is copied there on the native file system.
set -eu
sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
case "$1" in
clean)
  work=$2
  [ -f "$work/.x360n-posix-build" ] || { echo "not a distribution build: $work" >&2; exit 2; }
  if [ "$(cat "$work/.x360n-posix-build")" = copied ]; then rm -rf "${work:?}"; else rm -rf "${work:?}/build"; rm -f "$work/.x360n-posix-build"; fi
  exit 0 ;;
archive)
  # Deterministic tar.gz with explicit modes: drvfs reports every file as 0777.
  package=$2 work=$3 output=$4; shift 4
  [ -f "$work/.x360n-posix-build" ] || { echo "not a distribution build: $work" >&2; exit 2; }
  [ ! -e "$output" ] || { echo "archive already exists" >&2; exit 2; }
  name=$(basename "$package")
  stage="$work/archive"
  rm -rf "$stage"; mkdir -p "$stage"
  cp -R "$package" "$stage/$name"
  find "$stage/$name" -type d -exec chmod 0755 {} +
  find "$stage/$name" -type f -exec chmod 0644 {} +
  for exe in "$@"; do chmod 0755 "$stage/$name/$exe"; done
  tar -C "$stage" --sort=name --mtime=@946684800 --owner=0 --group=0 --numeric-owner --format=gnu -cf - "$name" | gzip -n -9 > "$output.partial"
  mv "$output.partial" "$output"
  rm -rf "$stage"
  exit 0 ;;
build) ;;
*) echo "usage: build|archive|clean" >&2; exit 2 ;;
esac
platform=$2 export_tree=$3 work=$4 evidence=$5 version=$6 payload=$7 tools=$8
source="$work/source" build="$work/build"
[ ! -e "$build" ] || { echo "build directory already exists" >&2; exit 2; }
cmake="$tools/cmake/bin/cmake" ctest="$tools/cmake/bin/ctest" ninja="$tools/ninja/ninja"
case "$platform" in
linux-x64)
  llvm="$tools/llvm" sysroot="$tools/sysroot-jammy/root"
  cc="$llvm/bin/clang" cxx="$llvm/bin/clang++"
  for tool in "$cc" "$cxx" "$llvm/bin/ld.lld" "$llvm/bin/llvm-ar" "$llvm/bin/llvm-ranlib" "$cmake" "$ctest" "$ninja"; do
    [ -x "$tool" ] || { echo "toolchain component missing: $tool" >&2; exit 3; }
  done
  # The glibc floor comes from this sysroot: refuse one that is not the locked set.
  cmp -s "$export_tree/scripts/packaging/linux-sysroot-jammy.lock" "$tools/sysroot-jammy/sysroot.lock" ||
    { echo "sysroot differs from linux-sysroot-jammy.lock: $tools/sysroot-jammy" >&2; exit 3; }
  # The sysroot's wayland-scanner runs on the build host; a host one only if it cannot.
  scanner="$sysroot/usr/bin/wayland-scanner"
  "$scanner" --version >/dev/null 2>&1 || scanner=$(command -v wayland-scanner) ||
    { echo "wayland-scanner unavailable" >&2; exit 3; }
  pkgconfig=${PKG_CONFIG:-$(command -v pkg-config)} || { echo "pkg-config unavailable" >&2; exit 3; }
  export PKG_CONFIG_SYSROOT_DIR="$sysroot"
  export PKG_CONFIG_LIBDIR="$sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:$sysroot/usr/share/pkgconfig:$sysroot/usr/lib/pkgconfig"
  flags="-march=x86-64-v2"
  # The C++ runtime is linked statically, like the static CRT on Windows; glibc stays the system's.
  links="-fuse-ld=lld -static-libstdc++ -static-libgcc"
  set -- "-DCMAKE_SYSROOT=$sysroot" "-DCMAKE_AR=$llvm/bin/llvm-ar" "-DCMAKE_RANLIB=$llvm/bin/llvm-ranlib" \
    "-DPKG_CONFIG_EXECUTABLE=$pkgconfig" "-DWAYLAND_SCANNER=$scanner"
  launcher=src/desktop/x360-native mspack=src/content/libx360n-mspack.so ;;
macos-arm64)
  # Apple clang of the selected Xcode (DEVELOPER_DIR), against its macOS SDK.
  cc=$(xcrun -f clang) cxx=$(xcrun -f clang++) sdk=$(xcrun --show-sdk-path)
  for tool in "$cc" "$cxx" "$cmake" "$ctest" "$ninja"; do
    [ -x "$tool" ] || { echo "toolchain component missing: $tool" >&2; exit 3; }
  done
  flags="" links=""
  set -- -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 "-DCMAKE_OSX_SYSROOT=$sdk"
  launcher=src/desktop/x360-native.app/Contents/MacOS/x360-native mspack=src/content/libx360n-mspack.dylib ;;
*) echo "unsupported platform: $platform" >&2; exit 2 ;;
esac
mkdir -p "$work" "$evidence"
if [ "$export_tree" != "$source" ]; then
  [ ! -e "$source" ] || { echo "source directory already exists" >&2; exit 2; }
  echo copied > "$work/.x360n-posix-build"
  cp -R "$export_tree" "$source"
else
  echo native > "$work/.x360n-posix-build"
fi
# Only the pinned tools: no global packages, include or library paths.
export PATH="$(dirname "$cxx"):$tools/cmake/bin:$tools/ninja:/usr/bin:/bin"
unset CPATH LIBRARY_PATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LD_LIBRARY_PATH
flags="$flags -ffile-prefix-map=$source=/x360-source"
"$cmake" -S "$source" -B "$build" -G Ninja -DX360_DESKTOP_ONLY=ON -DX360_DISTRIBUTION=ON \
  "-DX360_PACKAGE_PAYLOAD_SHA256=$payload" "-DX360_VERSION=$version" -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_C_COMPILER=$cc" "-DCMAKE_CXX_COMPILER=$cxx" "-DCMAKE_MAKE_PROGRAM=$ninja" "$@" \
  "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags" "-DCMAKE_EXE_LINKER_FLAGS=$links" "-DCMAKE_SHARED_LINKER_FLAGS=$links" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
"$cmake" --build "$build" --target x360-native x360n_desktop_tests -- -k 0
# This isolated tree registers only distribution tests, never SDK/game suites.
"$ctest" --test-dir "$build" -R desktop.catalog --no-tests=error --output-on-failure
"$ctest" --test-dir "$build" -R desktop.update-recovery --no-tests=error --output-on-failure
cp "$build/$launcher" "$build/src/supervisor/x360n" "$build/src/content/x360n-extract" "$build/$mspack" \
  "$build/compile_commands.json" "$evidence/"
"$ninja" -C "$build" -t deps > "$evidence/ninja-deps.txt"
"$ninja" -C "$build" -t query "$launcher" src/supervisor/x360n src/content/x360n-extract "$mspack" > "$evidence/ninja-query.txt"
{
  echo "compiler_version=$("$cxx" --version | head -n 1)"
  echo "compiler_sha256=$(sha256 "$(realpath "$cxx")")"
  echo "source=$source"
  echo "build=$build"
  echo "tools=$tools"
  echo "cmake=$("$cmake" --version | head -n 1)"
  echo "ninja=$("$ninja" --version)"
  if [ "$platform" = linux-x64 ]; then
    echo "llvm=$llvm"
    echo "sysroot=$sysroot"
    echo "sysroot_lock_sha256=$(sha256 "$tools/sysroot-jammy/sysroot.lock")"
    echo "wayland_scanner=$("$scanner" --version 2>&1)"
  else
    echo "developer_dir=$(xcode-select -p)"
    echo "sdk=$sdk"
    echo "xcode_version=$(xcodebuild -version | tr '\n' ' ' | sed 's/ *$//')"
    echo "sdk_version=$(xcrun --show-sdk-version)"
  fi
} > "$evidence/toolchain.txt"
