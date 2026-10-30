"""Release identity and package verification; packaging stays in package-desktop.ps1."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tarfile
import tempfile
import zipfile


VERSION = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?")
# Every published version carries one complete package per supported system.
PLATFORMS = {
    "windows-x64": dict(archive=".zip", executables={"x360-native.exe", "app/x360n.exe", "app/x360n-extract.exe", "app/x360n-mspack.dll"}),
    "linux-x64": dict(archive=".tar.gz", executables={"x360-native", "app/x360n", "app/x360n-extract", "app/libx360n-mspack.so"}),
    "macos-arm64": dict(archive=".zip", executables={"x360-native", "app/x360n", "app/x360n-extract", "app/libx360n-mspack.dylib"}),
}
EXECUTABLES = PLATFORMS["windows-x64"]["executables"]
FORBIDDEN = re.compile(r"\.(?:xex|xexp|xtt|ttf|otf|stfs|iso|nand|key|pem|pfx|bin|dll|lib|pdb)$", re.I)
NATIVE = re.compile(r"\.(?:exe|dll|so|dylib)$", re.I)
NATIVE_MAGIC = (b"MZ", b"\x7fELF", b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def command(*args):
    return subprocess.check_output(args, text=True, timeout=120).strip()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def package_name(version, platform):
    return f"x360-native-{version}-{platform}"


def asset_names(version, platform):
    archive = package_name(version, platform) + PLATFORMS[platform]["archive"]
    return {archive, archive + ".sha256"}


def verify_release_commands(commands):
    native = [c for c in commands if re.search(r"\.(?:c|cc|cpp|cxx|c\+\+|m|mm)$", c["file"], re.I)]
    require(native, "Release compiler evidence is empty")
    for entry in native:
        flags = entry.get("command", "")
        require(re.search(r"(?:^|\s)-O3(?:\s|$)", flags) and
                re.search(r"(?:^|\s)-DNDEBUG(?:=1)?(?:\s|$)", flags) and
                not re.search(r"(?:^|\s)(?:-O(?:0|1|2|4|s|z|g|fast)|[-/]UNDEBUG|[-/]D_DEBUG(?:=\S+)?|"
                              r"[-/]D_ITERATOR_DEBUG_LEVEL=[12]|/M[DT]d)(?:\s|$)", flags) and
                not re.search(r"(?:libcpmtd|libcmtd|msvcrtd|ucrtd|vcruntimed)(?:\.lib)?(?:\s|$)", flags, re.I),
                f"Non-Release compiler configuration: {entry['file']}")


def remote_tag_commit(repository, tag):
    obj = json.loads(command("gh", "api", f"repos/{repository}/git/ref/tags/{tag}"))["object"]
    for _ in range(8):
        if obj["type"] == "commit":
            return obj["sha"]
        require(obj["type"] == "tag", "Remote release ref is not a commit/tag")
        obj = json.loads(command("gh", "api", f"repos/{repository}/git/tags/{obj['sha']}"))["object"]
    raise ValueError("Remote annotated tag nesting exceeds limit")


def safe_path(path):
    require(path and "\\" not in path and ":" not in path and
            not path.startswith("/") and
            all(part not in {"", ".", ".."} for part in path.split("/")),
            f"Unsafe archive/index path: {path}")


def release_identity(event, ref, supplied_version):
    require(event in {"push", "pull_request", "workflow_dispatch"}, "Unsupported release event")
    if event == "push" and ref.startswith("refs/tags/"):
        require(ref.startswith("refs/tags/v"), "Publication requires a v* tag push")
        version = ref.removeprefix("refs/tags/v")
    else:
        # Branch, pull request and manual runs only build artifacts; publish() refuses them.
        require(not ref.startswith("refs/tags/") or event == "workflow_dispatch", "Tag refs come from tag pushes")
        version = supplied_version
    require(VERSION.fullmatch(version or ""), "Version must be MAJOR.MINOR.PATCH[-prerelease], without v")
    return version


def notes_for(version):
    path = Path("docs/releases") / f"{version}.md"
    require(path.is_file(), f"Release notes missing: {path}")
    text = path.read_text(encoding="utf-8")
    require(text.startswith("# ") and len(text.splitlines()) > 1,
            "Release notes need a title and body")
    return path, text.splitlines()[0][2:].strip()


def is_tag_publication(event, ref):
    return event == "push" and ref.startswith("refs/tags/v")


def on_main(commit):
    return subprocess.run(["git", "merge-base", "--is-ancestor", commit, "refs/remotes/origin/main"], timeout=120).returncode == 0


def identity():
    event, ref, commit = (os.environ[name] for name in ("GITHUB_EVENT_NAME", "GITHUB_REF", "GITHUB_SHA"))
    platform = os.environ["PACKAGE_PLATFORM"]
    require(platform in PLATFORMS, f"Unsupported package platform: {platform}")
    require(command("git", "rev-parse", "HEAD") == commit, "Checkout differs from event commit")
    version = release_identity(event, ref, os.environ.get("PACKAGE_VERSION", ""))
    tag = ref.removeprefix("refs/tags/") if ref.startswith("refs/tags/") else None
    if tag:
        require(command("git", "rev-parse", "--verify", f"{ref}^{{commit}}") == commit,
                "Tag differs from event commit")
    notes_hash = None
    if is_tag_publication(event, ref):
        notes_hash = file_digest(notes_for(version)[0])
    provenance = dict(schema=1, version=version, commit=commit, tag=tag, ref=ref,
                      event=event, repository=os.environ["GITHUB_REPOSITORY"],
                      workflow_ref=os.environ["GITHUB_WORKFLOW_REF"],
                      run_id=os.environ["GITHUB_RUN_ID"], run_attempt=os.environ["GITHUB_RUN_ATTEMPT"],
                      run_url=f"{os.environ['GITHUB_SERVER_URL']}/{os.environ['GITHUB_REPOSITORY']}/actions/runs/{os.environ['GITHUB_RUN_ID']}",
                      runner_image=os.environ.get("ImageOS"), runner_image_version=os.environ.get("ImageVersion"),
                      release_notes_sha256=notes_hash)
    Path(".local/release").mkdir(parents=True, exist_ok=True)
    Path(".local/release/provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        output.write(f"version={version}\nartifact={package_name(version, platform)}\n")
    print(f"Exact source: {commit}; version: {version}; event: {event}; platform: {platform}")


def posix_tools(lock_text, platform):
    """Pinned tool identities of posix-toolchain.lock for one platform."""
    tools = []
    for line in lock_text.splitlines():
        fields = line.split()
        if fields and not line.startswith("#") and fields[0] == platform:
            require(len(fields) == 6 and re.fullmatch(r"[a-f0-9]{64}", fields[3]), "Malformed POSIX toolchain lock")
            tools.append(dict(id=fields[1], version=fields[2], sha256=fields[3]))
    require(tools, f"No pinned tools for {platform}")
    return tools


def expected_tools(platform, read):
    """Tool provenance a package must carry, from the locks in its own source payload."""
    if platform == "windows-x64":
        return [{k: t[k] for k in ("id", "version", "sha256")} for t in json.loads(read("scripts/setup/desktop-tools.lock.json"))["tools"]]
    tools = posix_tools(read("scripts/packaging/posix-toolchain.lock").decode("utf-8"), platform)
    if platform == "linux-x64":
        tools.append(dict(id="sysroot-jammy", version="ubuntu-22.04", sha256=digest(read("scripts/packaging/linux-sysroot-jammy.lock"))))
    return tools


def environment(platform, versions):
    require(platform in PLATFORMS, f"Unsupported package platform: {platform}")
    path = Path(".local/release/provenance.json")
    provenance = json.loads(path.read_text(encoding="utf-8"))
    tools = expected_tools(platform, lambda rel: Path(rel).read_bytes())
    if platform == "windows-x64":
        msvc, sdk = versions.strip().split(";")
        require(msvc and sdk, "Microsoft C++/Windows SDK environment missing")
        locked = {t["id"]: t for t in json.loads(Path("scripts/setup/desktop-tools.lock.json").read_text(encoding="utf-8"))["tools"]}
        for tool in tools:
            receipt = Path(".local/toolchains") / locked[tool["id"]]["directory"] / ".x360n-archive-sha256"
            require(receipt.read_text().strip() == tool["sha256"], f"Tool archive receipt differs: {tool['id']}")
        llvm = json.loads(Path("deps.lock.json").read_text(encoding="utf-8"))["llvm"]
        provenance["llvm_archive_sha256"] = llvm["sha256"]
        provenance["llvm_extractor"] = json.loads(Path(".local/release/llvm-extractor.json").read_text(encoding="utf-8"))
        provenance["msvc_tools_version"], provenance["windows_sdk_version"] = msvc, sdk.rstrip("\\")
    else:
        base = Path(".local/toolchains") / f"posix-{platform}"
        for tool in tools:
            if tool["id"] == "sysroot-jammy":
                prepared = (base / "sysroot-jammy" / "sysroot.lock").read_bytes()
                require(digest(prepared) == tool["sha256"], "Prepared sysroot differs from its lock")
            else:
                require((base / tool["id"] / ".x360n-sha256").read_text().strip() == tool["sha256"], f"Tool archive receipt differs: {tool['id']}")
        if platform == "macos-arm64":
            require(versions.startswith("Xcode "), "Xcode version missing")
            provenance["xcode"] = versions.strip()
    provenance["tools"] = tools
    path.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")


def read_zip(archive_path, name):
    files, modes = {}, {}
    with zipfile.ZipFile(archive_path) as archive:
        for entry in archive.infolist():
            safe_path(entry.filename)
            require(entry.filename.startswith(name + "/") and not entry.is_dir(), "Unexpected archive root/directory")
            rel = entry.filename[len(name) + 1:]
            require(rel.casefold() not in {p.casefold() for p in files}, "Duplicate archive path")
            mode = entry.external_attr >> 16
            require(mode & 0o170000 != 0o120000, "Symlink in archive")
            files[rel] = archive.read(entry)  # Includes ZIP CRC verification.
            modes[rel] = mode & 0o7777 if mode else None
    return files, modes


def read_tar(archive_path, name):
    files, modes = {}, {}
    with tarfile.open(archive_path, "r:gz") as archive:
        for entry in archive.getmembers():
            safe_path(entry.name)
            require(entry.name == name or entry.name.startswith(name + "/"), "Unexpected archive root")
            require(entry.uid == 0 and entry.gid == 0, "Archive entry not owned by 0/0")
            if entry.isdir():
                require(entry.mode & 0o7777 == 0o755, "Unexpected directory mode")
                continue
            require(entry.isfile(), "Link or special file in archive")
            rel = entry.name[len(name) + 1:]
            require(rel.casefold() not in {p.casefold() for p in files}, "Duplicate archive path")
            files[rel] = archive.extractfile(entry).read()
            modes[rel] = entry.mode & 0o7777
    return files, modes


def check_executable(platform, rel, data):
    if platform == "windows-x64":
        require(data.startswith(b"MZ") and len(data) >= 64, f"Missing/invalid executable: {rel}")
        offset = int.from_bytes(data[60:64], "little")
        require(data[offset:offset+6] == b"PE\0\0\x64\x86", f"Executable is not Windows x64: {rel}")
    elif platform == "linux-x64":
        require(data[:4] == b"\x7fELF" and len(data) >= 20, f"Missing/invalid executable: {rel}")
        require(data[4] == 2 and data[5] == 1 and int.from_bytes(data[18:20], "little") == 0x3E, f"Executable is not Linux x86-64: {rel}")
    else:
        require(data[:4] == b"\xcf\xfa\xed\xfe" and len(data) >= 32, f"Missing/invalid executable: {rel}")
        require(int.from_bytes(data[4:8], "little") == 0x0100000C, f"Executable is not macOS arm64: {rel}")


def verify(directory, version, commit, ref, run_id, platform="windows-x64", exact=True):
    require(VERSION.fullmatch(version), "Invalid expected version")
    require(platform in PLATFORMS, f"Unsupported package platform: {platform}")
    name = package_name(version, platform)
    executables = PLATFORMS[platform]["executables"]
    archive_path = directory / f"{name}{PLATFORMS[platform]['archive']}"
    checksum_path = Path(str(archive_path) + ".sha256")
    if exact:
        require({p.name for p in directory.iterdir() if p.is_file()} == {archive_path.name, checksum_path.name},
                "Expected exactly the archive and its SHA-256 deliverable")
    require(archive_path.is_file() and checksum_path.is_file(), "Expected exactly the archive and its SHA-256 deliverable")
    checksum = checksum_path.read_text(encoding="utf-8").strip()
    actual = file_digest(archive_path)
    require(checksum == f"{actual}  {archive_path.name}", "Archive SHA-256 mismatch or wrong checksum filename")
    if platform == "linux-x64":
        files, modes = read_tar(archive_path, name)
    else:
        files, modes = read_zip(archive_path, name)
    for rel, data in files.items():
        require((rel in executables or not FORBIDDEN.search(rel)) and not {".local", ".git", "external"}.intersection(PurePosixPath(rel).parts),
                f"Forbidden distribution content: {rel}")
        require("/generated/" not in rel or rel.endswith("/generated/rexglue.cmake"), "Generated original code in archive")
        require(data[:4] not in {b"XEX0", b"XEX1", b"XEX2", b"CON ", b"LIVE", b"PIRS"}, "Original executable/container in archive")
        require(not NATIVE.search(rel) or rel in executables, "Unexpected executable in archive")
        require(not data.startswith(NATIVE_MAGIC) or rel in executables, "Renamed executable in archive")
        if platform != "windows-x64":
            # POSIX packages must start without a chmod: 0755 binaries, 0644 data.
            require(modes[rel] == (0o755 if rel in executables else 0o644), f"Unexpected file mode: {rel}")
    for exe in executables:
        check_executable(platform, exe, files.get(exe, b""))

    def index(prefix, filename):
        seen = set()
        for line in files[prefix + filename].decode("utf-8").splitlines():
            match = re.fullmatch(r"([a-f0-9]{64})\t(.+)", line)
            require(match, f"Malformed {filename}")
            expected, rel = match.groups()
            safe_path(rel)
            require(rel not in seen and rel != filename, f"Duplicate/self entry in {filename}")
            seen.add(rel)
            require(prefix + rel in files and digest(files[prefix + rel]) == expected,
                    f"Index mismatch: {prefix}{rel}")
        require(seen == {p[len(prefix):] for p in files if p.startswith(prefix)} - {filename},
                f"Unindexed/missing file in {filename}")

    index("", "package.sha256")
    index("app/workspace/", "payload.sha256")
    metadata = json.loads(files["package.json"])
    verify_release_commands(json.loads(files["app/compile-commands.json"]))
    lock = json.loads(files["app/workspace/deps.lock.json"])
    inventory = json.loads(files["licenses/inventory.json"])
    require(metadata["version"] == version and metadata["commit"] == commit, "Package source/version differs")
    require(metadata["configuration"] == "Release" and metadata["platform"] == platform and metadata["desktop_only"] is True,
            "Package is not the Release desktop distribution")
    require(metadata["sdk_commit"] == lock["rexglue_sdk"]["commit"] == inventory["sdk_commit"], "SDK provenance differs")
    require(metadata["patches"] == [{"file": p["file"], "sha256": p["sha256"]} for p in lock["rexglue_sdk"]["patches"]], "Patch provenance differs")
    for patch in metadata["patches"]:
        require(digest(files["app/workspace/" + patch["file"]]) == patch["sha256"], "Bundled patch hash differs")
    expected_deps = {c["id"]: c["revision"] for c in inventory["components"] if c["id"] in inventory["desktop_dependencies"]}
    require(metadata["dependencies"] == expected_deps, "Desktop dependency provenance differs")
    require(digest(files["app/workspace/payload.sha256"]) == metadata["payload_sha256"], "Payload identity differs")
    for notice in ("LICENSE", "THIRD_PARTY_NOTICES.md", "licenses/inventory.json"):
        require(files[notice] == files["app/workspace/" + notice], "Legal material differs from source payload")
    provenance = metadata["provenance"]
    expected_tag = ref.removeprefix("refs/tags/") if ref.startswith("refs/tags/") else None
    require(provenance["commit"] == commit and provenance["version"] == version and provenance["ref"] == ref and
            provenance["run_id"] == str(run_id) and provenance["tag"] == expected_tag, "Actions provenance differs")
    if platform == "windows-x64":
        require(provenance["llvm_archive_sha256"] == lock["llvm"]["sha256"], "LLVM archive provenance differs")
    require(provenance["tools"] == expected_tools(platform, lambda rel: files["app/workspace/" + rel]), "Tool provenance differs")
    print(f"Verified {archive_path.name}: {actual}; {len(files)} files; exact commit {commit}")
    return metadata


def publish(directory=Path(".local/packages")):
    event, ref = os.environ["GITHUB_EVENT_NAME"], os.environ["GITHUB_REF"]
    require(event != "workflow_dispatch", "Manual runs cannot publish")
    require(is_tag_publication(event, ref), "Only v* tag pushes publish")
    version = release_identity(event, ref, "")
    commit, tag = os.environ["GITHUB_SHA"], ref.removeprefix("refs/tags/")
    require(command("git", "rev-parse", "HEAD") == commit and
            command("git", "rev-parse", "--verify", f"{ref}^{{commit}}") == commit, "Release tag/checkout differs")
    # An authorized tag points into the published history, never at an unmerged branch.
    require(on_main(commit), "Release tag is not on main")
    expected = set().union(*(asset_names(version, p) for p in PLATFORMS))
    require({p.name for p in directory.iterdir()} == expected, "Release needs exactly every platform package and checksum")
    notes, title = notes_for(version)
    repository = os.environ["GITHUB_REPOSITORY"]
    for platform in PLATFORMS:
        metadata = verify(directory, version, commit, ref, os.environ["GITHUB_RUN_ID"], platform, exact=False)
        require(metadata["provenance"]["release_notes_sha256"] == file_digest(notes), "Release notes differ from build commit")
        require(metadata["provenance"]["repository"] == repository and metadata["provenance"]["event"] == event,
                "Release repository/event provenance differs")
    require(remote_tag_commit(repository, tag) == commit, "Remote tag moved since the build event")
    assets = sorted(directory / name for name in expected)
    require(all(p.is_file() for p in assets), "Unexpected artifact directory")
    # gh refuses an existing release; never replace published or partial assets.
    command("gh", "release", "create", tag, *(str(p) for p in assets), "--repo", repository,
            "--verify-tag", "--target", commit, "--draft", "--title", title, "--notes-file", str(notes),
            *( ["--prerelease"] if "-" in version else []))
    release = json.loads(command("gh", "release", "view", tag, "--repo", repository, "--json", "isDraft,assets"))
    require(release["isDraft"] and {a["name"] for a in release["assets"]} == {p.name for p in assets}, "Draft assets incomplete")
    # Verify the actual bytes stored on GitHub before making anything public.
    with tempfile.TemporaryDirectory() as downloaded:
        command("gh", "release", "download", tag, "--repo", repository, "--dir", downloaded)
        for asset in assets:
            require(file_digest(Path(downloaded) / asset.name) == file_digest(asset), "Uploaded Release asset differs")
    require(remote_tag_commit(repository, tag) == commit, "Remote tag moved during upload")
    command("gh", "release", "edit", tag, "--repo", repository, "--draft=false",
            "--prerelease=" + str("-" in version).lower(), "--latest=" + str("-" not in version).lower())
    print(f"Published verified {'pre-release' if '-' in version else 'release'}: {tag} ({len(assets)} assets)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="mode", required=True)
    commands.add_parser("identity")
    env = commands.add_parser("environment")
    env.add_argument("--platform", required=True)
    env.add_argument("--versions", required=True)
    check = commands.add_parser("verify")
    for name in ("directory", "version", "commit", "ref", "run-id", "platform"):
        check.add_argument("--" + name, required=True)
    commands.add_parser("publish")
    args = parser.parse_args()
    if args.mode == "identity":
        identity()
    elif args.mode == "environment":
        environment(args.platform, args.versions)
    elif args.mode == "verify":
        verify(Path(args.directory), args.version, args.commit, args.ref, args.run_id, args.platform)
    else:
        publish()


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, zipfile.BadZipFile, tarfile.TarError, subprocess.SubprocessError) as exc:
        print(f"Release gate failed: {exc}", file=sys.stderr)
        sys.exit(1)
