"""Release identity and ZIP verification; packaging stays in package-desktop.ps1."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import zipfile


VERSION = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?")
EXECUTABLES = {"x360-native.exe", "app/x360n.exe", "app/x360n-extract.exe"}
FORBIDDEN = re.compile(r"\.(?:xex|xexp|xtt|ttf|otf|stfs|iso|nand|key|pem|pfx|bin|dll|lib|pdb)$", re.I)


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
            f"Unsafe ZIP/index path: {path}")


def release_identity(event, ref, supplied_version):
    require(event in {"push", "workflow_dispatch"}, "Unsupported release event")
    if event == "push":
        require(ref.startswith("refs/tags/v"), "Publication requires a v* tag push")
        version = ref.removeprefix("refs/tags/v")
    else:
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


def identity():
    event, ref, commit = (os.environ[name] for name in ("GITHUB_EVENT_NAME", "GITHUB_REF", "GITHUB_SHA"))
    require(command("git", "rev-parse", "HEAD") == commit, "Checkout differs from event commit")
    version = release_identity(event, ref, os.environ.get("PACKAGE_VERSION", ""))
    tag = ref.removeprefix("refs/tags/") if ref.startswith("refs/tags/") else None
    if tag:
        require(command("git", "rev-parse", "--verify", f"{ref}^{{commit}}") == commit,
                "Tag differs from event commit")
    notes_hash = None
    if event == "push":
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
        output.write(f"version={version}\nartifact=x360-native-{version}-windows-x64\n")
    print(f"Exact source: {commit}; version: {version}; event: {event}")


def environment(versions):
    msvc, sdk = versions.strip().split(";")
    require(msvc and sdk, "Microsoft C++/Windows SDK environment missing")
    path = Path(".local/release/provenance.json")
    provenance = json.loads(path.read_text(encoding="utf-8"))
    tools = json.loads(Path("scripts/setup/desktop-tools.lock.json").read_text(encoding="utf-8"))["tools"]
    provenance["tools"] = []
    for tool in tools:
        receipt = Path(".local/toolchains") / tool["directory"] / ".x360n-archive-sha256"
        require(receipt.read_text().strip() == tool["sha256"], f"Tool archive receipt differs: {tool['id']}")
        provenance["tools"].append({key: tool[key] for key in ("id", "version", "sha256")})
    llvm = json.loads(Path("deps.lock.json").read_text(encoding="utf-8"))["llvm"]
    provenance["llvm_archive_sha256"] = llvm["sha256"]
    provenance["llvm_extractor"] = json.loads(Path(".local/release/llvm-extractor.json").read_text(encoding="utf-8"))
    provenance["msvc_tools_version"], provenance["windows_sdk_version"] = msvc, sdk.rstrip("\\")
    path.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")


def verify(directory, version, commit, ref, run_id):
    require(VERSION.fullmatch(version), "Invalid expected version")
    name = f"x360-native-{version}-windows-x64"
    archive_path = directory / f"{name}.zip"
    checksum_path = directory / f"{name}.zip.sha256"
    require({p.name for p in directory.iterdir() if p.is_file()} == {archive_path.name, checksum_path.name},
            "Expected exactly the ZIP and its SHA-256 deliverable")
    checksum = checksum_path.read_text(encoding="utf-8").strip()
    actual = file_digest(archive_path)
    require(checksum == f"{actual}  {archive_path.name}", "ZIP SHA-256 mismatch or wrong checksum filename")
    with zipfile.ZipFile(archive_path) as archive:
        files = {}
        for entry in archive.infolist():
            safe_path(entry.filename)
            require(entry.filename.startswith(name + "/") and not entry.is_dir(), "Unexpected ZIP root/directory")
            rel = entry.filename[len(name) + 1:]
            require(rel.casefold() not in {p.casefold() for p in files}, "Duplicate ZIP path")
            require((entry.external_attr >> 16) & 0o170000 != 0o120000, "Symlink in ZIP")
            require(not FORBIDDEN.search(rel) and not {".local", ".git", "external"}.intersection(PurePosixPath(rel).parts),
                    f"Forbidden distribution content: {rel}")
            require("/generated/" not in rel or rel.endswith("/generated/rexglue.cmake"), "Generated original code in ZIP")
            data = archive.read(entry)  # Includes ZIP CRC verification.
            require(data[:4] not in {b"XEX0", b"XEX1", b"XEX2", b"CON ", b"LIVE", b"PIRS"}, "Original executable/container in ZIP")
            require(not rel.lower().endswith(".exe") or rel in EXECUTABLES, "Unexpected executable in ZIP")
            require(not data.startswith(b"MZ") or rel in EXECUTABLES, "Renamed executable in ZIP")
            files[rel] = data
        for exe in EXECUTABLES:
            data = files.get(exe, b"")
            require(data.startswith(b"MZ") and len(data) >= 64, f"Missing/invalid executable: {exe}")
            offset = int.from_bytes(data[60:64], "little")
            require(data[offset:offset+6] == b"PE\0\0\x64\x86", f"Executable is not Windows x64: {exe}")

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
        require(metadata["configuration"] == "Release" and metadata["platform"] == "windows-x64" and metadata["desktop_only"] is True,
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
        require(provenance["llvm_archive_sha256"] == lock["llvm"]["sha256"], "LLVM archive provenance differs")
        tools = json.loads(files["app/workspace/scripts/setup/desktop-tools.lock.json"])["tools"]
        require(provenance["tools"] == [{k: t[k] for k in ("id", "version", "sha256")} for t in tools], "Tool provenance differs")
    print(f"Verified {archive_path.name}: {actual}; {len(files)} files; exact commit {commit}")
    return metadata


def publish():
    event, ref = os.environ["GITHUB_EVENT_NAME"], os.environ["GITHUB_REF"]
    require(event == "push", "Manual runs cannot publish")
    version = release_identity(event, ref, "")
    commit, tag = os.environ["GITHUB_SHA"], ref.removeprefix("refs/tags/")
    require(command("git", "rev-parse", "HEAD") == commit and
            command("git", "rev-parse", "--verify", f"{ref}^{{commit}}") == commit, "Release tag/checkout differs")
    directory = Path(".local/packages")
    metadata = verify(directory, version, commit, ref, os.environ["GITHUB_RUN_ID"])
    notes, title = notes_for(version)
    require(metadata["provenance"]["release_notes_sha256"] == file_digest(notes), "Release notes differ from build commit")
    repository = os.environ["GITHUB_REPOSITORY"]
    require(metadata["provenance"]["repository"] == repository and metadata["provenance"]["event"] == event,
            "Release repository/event provenance differs")
    require(remote_tag_commit(repository, tag) == commit, "Remote tag moved since the build event")
    assets = sorted(directory.iterdir())
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
    print(f"Published verified {'pre-release' if '-' in version else 'release'}: {tag}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="mode", required=True)
    commands.add_parser("identity")
    env = commands.add_parser("environment")
    env.add_argument("--versions", required=True)
    check = commands.add_parser("verify")
    for name in ("directory", "version", "commit", "ref", "run-id"):
        check.add_argument("--" + name, required=True)
    commands.add_parser("publish")
    args = parser.parse_args()
    if args.mode == "identity":
        identity()
    elif args.mode == "environment":
        environment(args.versions)
    elif args.mode == "verify":
        verify(Path(args.directory), args.version, args.commit, args.ref, args.run_id)
    else:
        publish()


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, zipfile.BadZipFile, subprocess.SubprocessError) as exc:
        print(f"Release gate failed: {exc}", file=sys.stderr)
        sys.exit(1)
