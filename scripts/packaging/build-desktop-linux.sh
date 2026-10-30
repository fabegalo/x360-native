#!/bin/sh
# Linux x64 distribution build for package-desktop.ps1 -Platform linux-x64.
# Builds the exported, patched source with the locked LLVM and a private sysroot,
# runs only the distribution tests and writes binaries plus raw compiler/linker
# evidence to <evidence>. Never installs, publishes or touches the user's data.
#   build <exported-source> <evidence> <job> <version> <payload-sha256> <toolchain> <llvm-version>
#   archive <package-dir> <job> <output.tar.gz> <executable>...
#   clean <job>
set -eu
cache="$HOME/.cache/x360n-distribution"
case "$1" in
clean)
  case "$2" in *[!0-9a-f]* | "") echo "invalid job" >&2; exit 2 ;; esac
  [ -f "$cache/$2/.x360n-linux-build" ] || { echo "not a distribution build: $cache/$2" >&2; exit 2; }
  rm -rf "${cache:?}/$2"
  exit 0 ;;
archive)
  # Deterministic tar.gz with explicit modes: drvfs reports every file as 0777.
  package=$2 job=$3 output=$4; shift 4
  case "$job" in *[!0-9a-f]* | "") echo "invalid job" >&2; exit 2 ;; esac
  [ ! -e "$output" ] || { echo "archive already exists" >&2; exit 2; }
  name=$(basename "$package")
  stage="$cache/$job/archive"
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
export_tree=$2 evidence=$3 job=$4 version=$5 payload=$6 toolchain=$7 llvm_version=$8
case "$job" in *[!0-9a-f]* | "") echo "invalid job" >&2; exit 2 ;; esac
sysroot="$toolchain/root"
llvm="$toolchain/llvm-$llvm_version-linux-x64"
source="$cache/$job/source" build="$cache/$job/build"
for tool in "$llvm/bin/clang" "$llvm/bin/clang++" "$sysroot/usr/bin/cmake" "$sysroot/usr/bin/ctest" "$sysroot/usr/bin/ninja" "$sysroot/usr/bin/pkg-config"; do
  [ -x "$tool" ] || { echo "toolchain component missing: $tool" >&2; exit 3; }
done
[ ! -e "$cache/$job" ] || { echo "build directory already exists" >&2; exit 2; }
mkdir -p "$build" "$evidence"
: > "$cache/$job/.x360n-linux-build"
# Source and build stay siblings on ext4, like the Windows job: the distribution
# compiler launcher passes the source path relative to the build directory.
cp -R "$export_tree" "$source"
# Only the private toolchain: no global packages, include or library paths.
export PATH="$llvm/bin:$sysroot/usr/bin:/usr/bin:/bin"
export LD_LIBRARY_PATH="$sysroot/usr/lib/x86_64-linux-gnu:$sysroot/lib/x86_64-linux-gnu"
export PKG_CONFIG_SYSROOT_DIR="$sysroot"
export PKG_CONFIG_LIBDIR="$sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:$sysroot/usr/share/pkgconfig:$sysroot/usr/lib/pkgconfig"
unset CPATH LIBRARY_PATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH
flags="-march=x86-64-v2 -ffile-prefix-map=$source=/x360-source"
# The C++ runtime is linked statically, like the static CRT on Windows; glibc stays the system's.
links="-static-libstdc++ -static-libgcc"
cmake -S "$source" -B "$build" -G Ninja -DX360_DESKTOP_ONLY=ON -DX360_DISTRIBUTION=ON \
  "-DX360_PACKAGE_PAYLOAD_SHA256=$payload" "-DX360_VERSION=$version" -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_C_COMPILER=$llvm/bin/clang" "-DCMAKE_CXX_COMPILER=$llvm/bin/clang++" "-DCMAKE_SYSROOT=$sysroot" \
  "-DCMAKE_MAKE_PROGRAM=$sysroot/usr/bin/ninja" "-DPKG_CONFIG_EXECUTABLE=$sysroot/usr/bin/pkg-config" \
  "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags" "-DCMAKE_EXE_LINKER_FLAGS=$links" "-DCMAKE_SHARED_LINKER_FLAGS=$links" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$build" --target x360-native x360n_desktop_tests
# This isolated tree registers only distribution tests, never SDK/game suites.
ctest --test-dir "$build" -R desktop.catalog --no-tests=error --output-on-failure
ctest --test-dir "$build" -R desktop.update-recovery --no-tests=error --output-on-failure
cp "$build/src/desktop/x360-native" "$build/src/supervisor/x360n" "$build/src/content/x360n-extract" \
  "$build/src/content/libx360n-mspack.so" "$build/compile_commands.json" "$evidence/"
ninja -C "$build" -t deps > "$evidence/ninja-deps.txt"
ninja -C "$build" -t query src/desktop/x360-native src/supervisor/x360n src/content/x360n-extract src/content/libx360n-mspack.so > "$evidence/ninja-query.txt"
clang="$(readlink -f "$llvm/bin/clang++")"
{
  echo "compiler_version=$("$llvm/bin/clang++" --version | head -n 1)"
  echo "compiler_sha256=$(sha256sum "$clang" | cut -d' ' -f1)"
  echo "source=$source"
  echo "build=$build"
  echo "llvm=$llvm"
  echo "sysroot=$sysroot"
} > "$evidence/toolchain.txt"
