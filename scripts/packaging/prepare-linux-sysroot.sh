#!/bin/sh
# Private Ubuntu 22.04 target sysroot for package-desktop.ps1 -Platform linux-x64/linux-arm64.
# Downloads exactly the .deb files pinned in <lock> from the official archive into
# a private apt state, checks each SHA-256 and extracts them under <dest>/root.
# Installs nothing on the host. Build tools (CMake, Ninja, pkg-config) are not
# part of it: they stay in the validation toolchain and run on the host.
#   prepare-linux-sysroot.sh <dest> <lock> [amd64|arm64]
set -eu
dest=$1 lock=$2 arch=${3:-amd64}
case "$arch" in
amd64) archive=http://archive.ubuntu.com/ubuntu security=http://security.ubuntu.com/ubuntu ;;
arm64) archive=http://ports.ubuntu.com/ubuntu-ports security=http://ports.ubuntu.com/ubuntu-ports ;;
*) echo "unsupported sysroot architecture: $arch" >&2; exit 2 ;;
esac
[ ! -e "$dest" ] || { echo "sysroot already exists: $dest" >&2; exit 2; }
keyring=/usr/share/keyrings/ubuntu-archive-keyring.gpg
[ -f "$keyring" ] || { echo "Ubuntu archive keyring missing: $keyring" >&2; exit 3; }
apt="$dest/apt"
mkdir -p "$apt/etc/apt/apt.conf.d" "$apt/etc/apt/preferences.d" "$apt/var/lib/apt/lists/partial" \
  "$apt/var/cache/apt/archives/partial" "$apt/var/lib/dpkg" "$dest/debs" "$dest/root"
: > "$apt/var/lib/dpkg/status"
cat > "$apt/etc/apt/sources.list" <<EOF
deb [arch=$arch signed-by=$keyring] $archive jammy main universe
deb [arch=$arch signed-by=$keyring] $archive jammy-updates main universe
deb [arch=$arch signed-by=$keyring] $security jammy-security main universe
EOF
set -- -o "Dir=$apt" -o "Dir::State::status=$apt/var/lib/dpkg/status" -o Dir::Etc::SourceParts=/nonexistent \
  -o APT::Architecture=$arch -o APT::Architectures::=$arch -o Acquire::Languages=none -o APT::Sandbox::User=root
apt-get "$@" -q update
pins=$(tr -d '\r' < "$lock" | grep -v '^#' | awk 'NF == 3 { print $1 "=" $2 }')
(cd "$dest/debs" && apt-get "$@" -q download $pins)
tr -d '\r' < "$lock" | grep -v '^#' | awk 'NF == 3' | while read -r package version sha; do
  deb=$(ls "$dest/debs/${package}_"*.deb)
  [ "$(dpkg-deb -f "$deb" Package)" = "$package" ] && [ "$(dpkg-deb -f "$deb" Version)" = "$version" ] ||
    { echo "unexpected package: $deb" >&2; exit 4; }
  [ "$(sha256sum "$deb" | cut -d' ' -f1)" = "$sha" ] || { echo "SHA-256 mismatch: $deb" >&2; exit 4; }
  dpkg-deb -x "$deb" "$dest/root"
done
[ "$(ls "$dest/debs" | wc -l)" -eq "$(tr -d '\r' < "$lock" | grep -v '^#' | awk 'NF == 3' | wc -l)" ] ||
  { echo "downloaded set differs from the lock" >&2; exit 4; }
# Absolute links (libcrypt.so -> /lib/...) must resolve inside the sysroot, never on the host.
find "$dest/root" -type l -lname '/*' | while read -r link; do
  ln -sfn "$(realpath -m --relative-to="$(dirname "$link")" "$dest/root$(readlink "$link")")" "$link"
done
tr -d '\r' < "$lock" > "$dest/sysroot.lock"
rm -rf "$apt"
echo "sysroot ready: $dest/root"
