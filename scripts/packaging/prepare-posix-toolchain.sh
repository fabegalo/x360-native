#!/bin/sh
# Pinned build tools for the Linux (x64, ARM64) / macOS (ARM64, x64) desktop packages.
# Downloads each asset of posix-toolchain.lock for <platform>, checks its SHA-256
# and extracts it under <dest>/<id>; on Linux also prepares the Ubuntu 22.04
# target sysroot (prepare-linux-sysroot.sh) under <dest>/sysroot-jammy.
# A component whose receipt already names the locked digest is reused, so a
# restored CI cache costs nothing. Installs nothing outside <dest>.
#   prepare-posix-toolchain.sh <platform> <dest>
set -eu
platform=$1 dest=$2
here=$(cd "$(dirname "$0")" && pwd)
lock="$here/posix-toolchain.lock"
case "$platform" in linux-x64 | linux-arm64 | macos-arm64 | macos-x64) ;; *) echo "unsupported platform: $platform" >&2; exit 2 ;; esac
mkdir -p "$dest"
dest=$(cd "$dest" && pwd)
sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
tr -d '\r' < "$lock" | grep -v '^#' | awk -v p="$platform" '$1 == p && NF == 6' | while read -r _ id version sha root url; do
  receipt="$dest/$id/.x360n-sha256"
  if [ -f "$receipt" ] && [ "$(cat "$receipt")" = "$sha" ]; then echo "$id $version: cached"; continue; fi
  rm -rf "$dest/$id" "$dest/.work"
  mkdir -p "$dest/.work/x"
  archive="$dest/.work/$(basename "$url")"
  curl -fsSL --retry 3 --retry-delay 5 -o "$archive" "$url"
  [ "$(sha256 "$archive")" = "$sha" ] || { echo "SHA-256 mismatch: $url" >&2; exit 4; }
  case "$archive" in
    *.zip) unzip -q "$archive" -d "$dest/.work/x" ;;
    *.tar.xz)
      # Only the compiler, linker and binary tools the package build uses: the
      # full LLVM tree is several GB. Missing members are caught by the checks below.
      xz -dc "$archive" | tar -x -C "$dest/.work/x" --wildcards --no-anchored -f - \
        'bin/clang' 'bin/clang++' 'bin/clang-[0-9]*' 'bin/lld' 'bin/ld.lld' 'bin/llvm-ar' 'bin/llvm-ranlib' \
        'bin/llvm-nm' 'bin/llvm-readobj' 'bin/llvm-readelf' 'bin/llvm-objcopy' 'bin/llvm-strip' 'lib/clang/*' || true ;;
    *) tar -xzf "$archive" -C "$dest/.work/x" ;;
  esac
  if [ "$root" = . ]; then mv "$dest/.work/x" "$dest/$id"; else mv "$dest/.work/x/$root" "$dest/$id"; fi
  rm -rf "$dest/.work"
  case "$id" in
    llvm) for tool in clang clang++ ld.lld llvm-ar llvm-ranlib llvm-readobj; do "$dest/llvm/bin/$tool" --version >/dev/null; done
          "$dest/llvm/bin/clang++" --version | head -n 1 | grep -q " $version" || { echo "LLVM version differs from the lock" >&2; exit 4; } ;;
    cmake) "$dest/cmake/bin/cmake" --version | head -n 1 | grep -q " $version$" || { echo "CMake version differs from the lock" >&2; exit 4; } ;;
    ninja) [ "$("$dest/ninja/ninja" --version)" = "$version" ] || { echo "Ninja version differs from the lock" >&2; exit 4; } ;;
  esac
  printf '%s\n' "$sha" > "$receipt"
  echo "$id $version: ready"
done
case "$platform" in
linux-x64) sysroot_lock="$here/linux-sysroot-jammy.lock" sysroot_arch=amd64 ;;
linux-arm64) sysroot_lock="$here/linux-sysroot-jammy-arm64.lock" sysroot_arch=arm64 ;;
*) sysroot_lock="" sysroot_arch="" ;;
esac
if [ -n "$sysroot_lock" ]; then
  if ! cmp -s "$dest/sysroot-jammy/sysroot.lock" "$sysroot_lock" 2>/dev/null &&
     ! tr -d '\r' < "$sysroot_lock" | cmp -s - "$dest/sysroot-jammy/sysroot.lock" 2>/dev/null; then
    rm -rf "$dest/sysroot-jammy"
    sh "$here/prepare-linux-sysroot.sh" "$dest/sysroot-jammy" "$sysroot_lock" "$sysroot_arch"
  else
    echo "sysroot-jammy: cached"
  fi
fi
