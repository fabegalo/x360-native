"""Synthetic release-gate tests; no original Xbox content or native execution."""

import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import release


POSIX_LOCK = ("# synthetic\n"
              f"linux-x64 llvm 22.1.8 {'1' * 64} LLVM https://example.invalid/llvm.tar.xz\n"
              f"linux-x64 cmake 3.31.6 {'2' * 64} cmake https://example.invalid/cmake.tar.gz\n"
              f"macos-arm64 cmake 3.31.6 {'3' * 64} cmake https://example.invalid/cmake.tar.gz\n"
              f"linux-arm64 llvm 22.1.8 {'6' * 64} LLVM https://example.invalid/llvm.tar.xz\n"
              f"linux-arm64 cmake 3.31.6 {'7' * 64} cmake https://example.invalid/cmake.tar.gz\n"
              f"macos-x64 cmake 3.31.6 {'3' * 64} cmake https://example.invalid/cmake.tar.gz\n")
SYSROOT_LOCK = b"# synthetic jammy lock\nlibc6 2.35 " + b"4" * 64 + b"\n"
SYSROOT_ARM64_LOCK = b"# synthetic jammy arm64 lock\nlibc6 2.35 " + b"8" * 64 + b"\n"
# Locked LLVM archive digest of each Windows platform in the synthetic deps.lock.json.
WINDOWS_LLVM_SHA256 = {"windows-x64": "e" * 64, "windows-arm64": "f" * 64}


def native_image(platform, machine=None):
    machine = release.PLATFORMS[platform]["machine"] if machine is None else machine
    if platform.startswith("windows-"):
        image = bytearray(128)
        image[:2] = b"MZ"
        image[60:64] = (64).to_bytes(4, "little")
        image[64:68] = b"PE\0\0"
        image[68:70] = machine.to_bytes(2, "little")
    elif platform.startswith("linux-"):
        image = bytearray(64)
        image[:6] = b"\x7fELF\x02\x01"
        image[18:20] = machine.to_bytes(2, "little")
    else:
        image = bytearray(64)
        image[:4] = b"\xcf\xfa\xed\xfe"
        image[4:8] = machine.to_bytes(4, "little")
    return bytes(image)


def other_architecture(platform):
    """The same system's package for the other architecture."""
    return platform.replace("-x64", "-arm64") if platform.endswith("-x64") else platform.replace("-arm64", "-x64")


class ReleaseTests(unittest.TestCase):
    platform = "windows-x64"

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.version = "0.1.0-preview.21"
        self.commit = "a" * 40
        self.ref = "refs/tags/v" + self.version
        self.name = release.package_name(self.version, self.platform)
        self.archive = self.directory / (self.name + release.PLATFORMS[self.platform]["archive"])
        self.executables = release.PLATFORMS[self.platform]["executables"]
        self.files = {exe: native_image(self.platform) for exe in self.executables}
        self.modes = {}
        self.files["app/compile-commands.json"] = json.dumps([
            dict(file="<source>/src/supervisor/main.cpp", command="clang++ -O3 -DNDEBUG --dependent-lib=libcmt -c main.cpp")
        ]).encode()
        self.files.update({"LICENSE": b"synthetic license", "THIRD_PARTY_NOTICES.md": b"synthetic notice"})
        patch_bytes = b"synthetic patch fixture"
        patches = [{"file": "patches/rexglue-sdk/fixture.patch", "sha256": release.digest(patch_bytes)}]
        inventory = {"sdk_commit": "b" * 40, "components": [{"id": "fmt", "revision": "c" * 40}], "desktop_dependencies": ["fmt"]}
        tools = [dict(id="ninja", version="1.13.1", sha256="d" * 64),
                 dict(platform="windows-arm64", id="ninja", version="1.13.1", sha256="9" * 64)]
        self.files["app/workspace/scripts/setup/desktop-tools.lock.json"] = json.dumps({"tools": tools}).encode()
        self.files["app/workspace/scripts/packaging/posix-toolchain.lock"] = POSIX_LOCK.encode()
        self.files["app/workspace/scripts/packaging/linux-sysroot-jammy.lock"] = SYSROOT_LOCK
        self.files["app/workspace/scripts/packaging/linux-sysroot-jammy-arm64.lock"] = SYSROOT_ARM64_LOCK
        tools = release.expected_tools(self.platform, lambda rel: self.files["app/workspace/" + rel])
        provenance = dict(commit=self.commit, version=self.version, ref=self.ref, run_id="42",
                          tag="v" + self.version, llvm_archive_sha256=WINDOWS_LLVM_SHA256.get(self.platform), tools=tools)
        self.metadata = dict(version=self.version, commit=self.commit, platform=self.platform, configuration="Release",
                             desktop_only=True, sdk_commit=inventory["sdk_commit"], patches=patches,
                             dependencies={"fmt": "c" * 40}, provenance=provenance)
        self.files["licenses/inventory.json"] = json.dumps(inventory).encode()
        for notice in ("LICENSE", "THIRD_PARTY_NOTICES.md", "licenses/inventory.json"):
            self.files["app/workspace/" + notice] = self.files[notice]
        self.files["app/workspace/" + patches[0]["file"]] = patch_bytes
        self.files["app/workspace/deps.lock.json"] = json.dumps({"rexglue_sdk": {"commit": inventory["sdk_commit"], "patches": patches}, "llvm": {"sha256": WINDOWS_LLVM_SHA256["windows-x64"]},
                                                                  "llvm_windows_arm64": {"sha256": WINDOWS_LLVM_SHA256["windows-arm64"]}}).encode()

    def mode(self, name):
        return self.modes.get(name, 0o755 if name in self.executables else 0o644)

    def write_archive(self):
        def index(prefix, name):
            return "".join(f"{release.digest(data)}\t{path[len(prefix):]}\n" for path, data in sorted(self.files.items())
                           if path.startswith(prefix) and path != prefix + name).encode()
        self.files["app/workspace/payload.sha256"] = index("app/workspace/", "payload.sha256")
        self.metadata["payload_sha256"] = release.digest(self.files["app/workspace/payload.sha256"])
        self.files["package.json"] = json.dumps(self.metadata).encode()
        self.files["package.sha256"] = index("", "package.sha256")
        if self.archive.name.endswith(".tar.gz"):
            with tarfile.open(self.archive, "w:gz") as archive:
                for name, data in self.files.items():
                    entry = tarfile.TarInfo(self.name + "/" + name)
                    entry.size, entry.mode = len(data), self.mode(name)
                    archive.addfile(entry, io.BytesIO(data))
        else:
            with zipfile.ZipFile(self.archive, "w") as archive:
                for name, data in self.files.items():
                    entry = zipfile.ZipInfo(self.name + "/" + name)
                    if not self.platform.startswith("windows-"):
                        entry.create_system, entry.external_attr = 3, (0o100000 | self.mode(name)) << 16
                    archive.writestr(entry, data)
        self.write_checksum()

    def write_checksum(self):
        Path(str(self.archive) + ".sha256").write_text(f"{release.file_digest(self.archive)}  {self.archive.name}\n")

    def verify(self):
        return release.verify(self.directory, self.version, self.commit, self.ref, "42", self.platform)

    def test_complete_archive(self):
        self.write_archive()
        self.assertEqual(self.verify()["commit"], self.commit)

    def test_reviewed_launcher_fonts(self):
        root = Path(__file__).resolve().parents[2]
        self.assertEqual(len(release.LAUNCHER_FONTS), 2)
        for name in release.LAUNCHER_FONTS:
            self.files[name] = (root / name.removeprefix("app/workspace/")).read_bytes()
        self.write_archive()
        self.assertEqual(self.verify()["commit"], self.commit)
        # Even internally consistent archive indices must not authorize changed bytes.
        name = next(iter(release.LAUNCHER_FONTS))
        self.files[name] += b"modified"
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "Forbidden"):
            self.verify()

    def test_debug_flags_rejected_even_with_release_metadata_and_valid_hashes(self):
        for flags in ("-O0 -DNDEBUG", "-O3 -DNDEBUG -D_DEBUG", "-O3 -DNDEBUG --dependent-lib=libcmtd",
                      "-O3 -DNDEBUG -O2", "-O3", "-O3 -DNDEBUG /MDd", "-O3 -DNDEBUG -D_ITERATOR_DEBUG_LEVEL=2",
                      "-O3 -DNDEBUG -UNDEBUG", "-O3 -DNDEBUG --dependent-lib=libcpmtd", "-O3 -DNDEBUG -Og"):
            with self.subTest(flags=flags):
                self.files["app/compile-commands.json"] = json.dumps([
                    dict(file="<source>/src/supervisor/main.cpp", command=f"clang++ {flags} -c main.cpp")
                ]).encode()
                self.write_archive()
                with self.assertRaisesRegex(ValueError, "Non-Release compiler"):
                    self.verify()

    def test_corrupt_download(self):
        self.write_archive()
        with self.archive.open("ab") as archive:
            archive.write(b"tampered")
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            self.verify()

    def test_incomplete_deliverables(self):
        self.write_archive()
        Path(str(self.archive) + ".sha256").unlink()
        with self.assertRaisesRegex(ValueError, "exactly"):
            self.verify()

    def test_self_consistent_forbidden_files(self):
        for name, data, reason in (("font.xtt", b"synthetic", "Forbidden"),
                                   ("app/workspace/font.otf", b"synthetic", "Forbidden"),
                                   ("app/workspace/font.ttf", b"synthetic", "Forbidden"),
                                   ("app/workspace/src/desktop/resources/fonts/NotoSansCJKjp-Regular.otf", b"synthetic", "Forbidden"),
                                   ("foreign.dll", b"MZ", "Forbidden"),
                                   ("app/foreign.so", b"synthetic", "Unexpected executable"),
                                   ("app/foreign.dylib", b"synthetic", "Unexpected executable"),
                                   ("renamed.dat", b"XEX2synthetic", "Original"),
                                   ("renamed.dat", b"MZsynthetic", "Renamed"),
                                   ("renamed.dat", b"\x7fELFsynthetic", "Renamed"),
                                   ("renamed.dat", b"\xcf\xfa\xed\xfesynthetic", "Renamed"),
                                   ("app/workspace/titles/fixture/generated/guest.cpp", b"synthetic", "Generated"),
                                   ("../escape.txt", b"synthetic", "Unsafe")):
            with self.subTest(name=name, data=data):
                self.files[name] = data
                self.write_archive()
                with self.assertRaisesRegex(ValueError, reason):
                    self.verify()
                del self.files[name]

    def test_duplicate_case_insensitive_path(self):
        self.files["license"] = b"duplicate"
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "Duplicate archive"):
            self.verify()

    def test_missing_and_wrong_architecture(self):
        exe = sorted(self.executables)[-1]
        original = self.files.pop(exe)
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "Missing/invalid"):
            self.verify()
        # The other architecture of the same system, and Windows x86 (0x14C).
        for machine in (release.PLATFORMS[other_architecture(self.platform)]["machine"], 0x14C):
            with self.subTest(machine=machine):
                self.files[exe] = native_image(self.platform, machine)
                self.write_archive()
                with self.assertRaisesRegex(ValueError, "is not"):
                    self.verify()
        self.files[exe] = original

    def test_only_this_platform_tools(self):
        def tools(platform):
            return release.expected_tools(platform, lambda rel: self.files["app/workspace/" + rel])
        if self.platform.startswith("macos-"):
            # The locked macOS CMake/Ninja are universal archives that serve both architectures.
            self.assertEqual(tools(self.platform), tools(other_architecture(self.platform)))
            return
        self.assertNotEqual(tools(self.platform), tools(other_architecture(self.platform)))
        self.metadata["provenance"]["tools"] = tools(other_architecture(self.platform))
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "Tool provenance"):
            self.verify()

    def test_wrong_build_provenance(self):
        for field, value, reason in (("commit", "wrong", "source/version"), ("configuration", "Debug", "Release"),
                                     ("platform", "linux-riscv64", "Release"),
                                     ("sdk_commit", "wrong", "SDK provenance"), ("dependencies", {}, "dependency provenance")):
            with self.subTest(field=field):
                previous = self.metadata[field]
                self.metadata[field] = value
                self.write_archive()
                with self.assertRaisesRegex(ValueError, reason):
                    self.verify()
                self.metadata[field] = previous

    def test_wrong_actions_provenance(self):
        for field in ("ref", "run_id", "tag", "commit"):
            with self.subTest(field=field):
                previous = self.metadata["provenance"][field]
                self.metadata["provenance"][field] = "wrong"
                self.write_archive()
                with self.assertRaisesRegex(ValueError, "Actions provenance"):
                    self.verify()
                self.metadata["provenance"][field] = previous

    def test_wrong_tool_provenance(self):
        self.metadata["provenance"]["tools"] = self.metadata["provenance"]["tools"][1:]
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "Tool provenance"):
            self.verify()

    def test_wrong_llvm_architecture(self):
        if not self.platform.startswith("windows-"):
            self.skipTest("Windows packages record the locked LLVM archive")
        self.metadata["provenance"]["llvm_archive_sha256"] = WINDOWS_LLVM_SHA256[other_architecture(self.platform)]
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "LLVM archive"):
            self.verify()

    def test_changed_patch_with_refreshed_indices(self):
        self.files["app/workspace/patches/rexglue-sdk/fixture.patch"] = b"changed patch"
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "patch hash"):
            self.verify()


class LinuxReleaseTests(ReleaseTests):
    platform = "linux-x64"

    def test_posix_modes(self):
        for name, mode in ((sorted(self.executables)[0], 0o644), ("LICENSE", 0o755)):
            with self.subTest(name=name):
                self.modes = {name: mode}
                self.write_archive()
                with self.assertRaisesRegex(ValueError, "file mode"):
                    self.verify()

    def test_links_and_owners_refused(self):
        self.write_archive()
        for kind in ("symlink", "owner"):
            with self.subTest(kind=kind):
                with tarfile.open(self.archive, "r:gz") as source:
                    members = [(m, source.extractfile(m).read() if m.isfile() else None) for m in source.getmembers()]
                with tarfile.open(self.archive, "w:gz") as target:
                    for member, data in members:
                        target.addfile(member, io.BytesIO(data) if data is not None else None)
                    extra = tarfile.TarInfo(self.name + "/app/linked")
                    if kind == "symlink":
                        extra.type, extra.linkname = tarfile.SYMTYPE, "/etc/passwd"
                        target.addfile(extra)
                    else:
                        extra.uid, extra.size = 1000, 1
                        target.addfile(extra, io.BytesIO(b"x"))
                self.write_checksum()
                with self.assertRaisesRegex(ValueError, "Link or special|owned by"):
                    self.verify()
                self.write_archive()

    def test_posix_tool_and_sysroot_provenance(self):
        ids = [tool["id"] for tool in self.metadata["provenance"]["tools"]]
        self.assertEqual(ids, ["llvm", "cmake", "sysroot-jammy"])
        lock = "app/workspace/scripts/packaging/" + release.LINUX_SYSROOTS[self.platform]
        self.files[lock] += b"libfoo 1 " + b"5" * 64 + b"\n"
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "Tool provenance"):
            self.verify()


class LinuxArm64ReleaseTests(LinuxReleaseTests):
    platform = "linux-arm64"


class MacReleaseTests(ReleaseTests):
    platform = "macos-arm64"

    def test_posix_modes(self):
        self.modes = {"app/x360n": 0o644}
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "file mode"):
            self.verify()


class MacX64ReleaseTests(MacReleaseTests):
    platform = "macos-x64"


class WindowsArm64ReleaseTests(ReleaseTests):
    platform = "windows-arm64"


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.version, self.commit = "0.1.0-preview.21", "a" * 40
        self.ref = "refs/tags/v" + self.version
        self.env = dict(GITHUB_EVENT_NAME="push", GITHUB_REF=self.ref, GITHUB_SHA=self.commit, GITHUB_RUN_ID="42", GITHUB_REPOSITORY="owner/repo")
        self.notes = self.directory.parent / f"{self.directory.name}-notes.md"
        self.notes.write_text("# Test release\n\nSynthetic notes.\n")
        self.addCleanup(self.notes.unlink)
        self.metadata = dict(provenance=dict(repository="owner/repo", event="push", release_notes_sha256=release.file_digest(self.notes)))
        for platform in release.PLATFORMS:
            for name in release.asset_names(self.version, platform):
                (self.directory / name).write_bytes(name.encode())

    def publish(self, command, verify=None, on_main=True):
        with patch.dict("os.environ", self.env), patch.object(release, "command", side_effect=command) as mocked, \
                patch.object(release, "verify", side_effect=verify or (lambda *a, **k: self.metadata)), \
                patch.object(release, "on_main", return_value=on_main), \
                patch.object(release, "notes_for", return_value=(self.notes, "Test release")), \
                patch.object(release, "remote_tag_commit", return_value=self.commit):
            try:
                release.publish(self.directory)
            finally:
                self.calls = [call.args for call in mocked.call_args_list]

    def git_only(self, *args):
        require_git = args[0] == "git"
        self.assertTrue(require_git, f"mutation before gates: {args}")
        return self.commit

    def test_versions_and_modes(self):
        self.assertEqual(release.release_identity("push", self.ref, "ignored"), self.version)
        self.assertEqual(release.release_identity("push", "refs/tags/v1.2.3", ""), "1.2.3")
        self.assertEqual(release.release_identity("workflow_dispatch", "refs/heads/main", "1.2.3-ci"), "1.2.3-ci")
        self.assertEqual(release.release_identity("push", "refs/heads/main", "1.2.3-ci.7"), "1.2.3-ci.7")
        self.assertEqual(release.release_identity("pull_request", "refs/pull/9/merge", "1.2.3-ci.7"), "1.2.3-ci.7")
        for event, ref, version in (("schedule", "refs/heads/main", "1.2.3"), ("pull_request", self.ref, "1.2.3"),
                                    ("push", "refs/tags/x1.2.3", "1.2.3"), ("push", "refs/heads/main", "../../escape"),
                                    ("workflow_dispatch", self.ref, "../../escape"), ("workflow_dispatch", self.ref, "v1.2.3")):
            with self.subTest(event=event, ref=ref, version=version), self.assertRaises(ValueError):
                release.release_identity(event, ref, version)

    def test_manual_and_branch_runs_cannot_publish(self):
        for event, ref in (("workflow_dispatch", self.ref), ("push", "refs/heads/main"), ("pull_request", "refs/pull/9/merge")):
            with self.subTest(event=event), patch.dict("os.environ", GITHUB_EVENT_NAME=event, GITHUB_REF=ref), \
                    patch.object(release, "command") as command:
                with self.assertRaisesRegex(ValueError, "Manual runs|Only v\\* tag"):
                    release.publish(self.directory)
                command.assert_not_called()

    def test_missing_release_notes(self):
        with self.assertRaisesRegex(ValueError, "notes missing"):
            release.notes_for("999.999.999")

    def test_remote_annotated_tag(self):
        with patch.object(release, "command", side_effect=[json.dumps({"object": {"type": "tag", "sha": "d" * 40}}),
                                                             json.dumps({"object": {"type": "commit", "sha": self.commit}})]):
            self.assertEqual(release.remote_tag_commit("owner/repo", "v1.2.3"), self.commit)

    def test_tag_outside_main_never_publishes(self):
        with self.assertRaisesRegex(ValueError, "not on main"):
            self.publish(self.git_only, on_main=False)

    def test_every_platform_required(self):
        for platform in release.PLATFORMS:
            with self.subTest(platform=platform):
                removed = sorted(release.asset_names(self.version, platform))[0]
                data = (self.directory / removed).read_bytes()
                (self.directory / removed).unlink()
                with self.assertRaisesRegex(ValueError, "every platform"):
                    self.publish(self.git_only)
                (self.directory / removed).write_bytes(data)

    def test_publish_has_no_mutation_before_gate_success(self):
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            self.publish(self.git_only, verify=ValueError("Incomplete artifact"))

    def test_incomplete_draft_never_publishes(self):
        def command(*args):
            if args[0] == "git":
                return self.commit
            if args[:3] == ("gh", "release", "view"):
                return json.dumps({"isDraft": True, "assets": []})
            return "draft-url"
        with self.assertRaisesRegex(ValueError, "Draft assets incomplete"):
            self.publish(command)
        self.assertFalse(any(call[:3] == ("gh", "release", "edit") for call in self.calls))

    def test_stable_bridge_includes_checksums_for_existing_launchers(self):
        self.version = "1.28.13"
        self.ref = "refs/tags/v" + self.version
        self.env["GITHUB_REF"] = self.ref
        self.directory = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.directory))
        self.metadata["provenance"]["release_notes_sha256"] = release.file_digest(self.notes)
        for platform in release.PLATFORMS:
            for name in release.asset_names(self.version, platform):
                (self.directory / name).write_bytes(name.encode())

        def command(*args):
            if args[0] == "git":
                return self.commit
            if args[:3] == ("gh", "release", "view"):
                return json.dumps({"isDraft": True, "assets": [
                    {"name": name} for platform in release.PLATFORMS
                    for name in sorted(release.asset_names(self.version, platform))
                ]})
            if args[:3] == ("gh", "release", "download"):
                destination = Path(args[args.index("--dir") + 1])
                for asset in self.directory.iterdir():
                    (destination / asset.name).write_bytes(asset.read_bytes())
            return "mocked"

        self.publish(command)
        create = next(c for c in self.calls if c[:3] == ("gh", "release", "create"))
        upload = [Path(a) for a in create if str(a).startswith(str(self.directory))]
        self.assertEqual(len(upload), 12)
        self.assertEqual(sum(a.name.endswith(".sha256") for a in upload), 6)
        self.assertIn("--latest=true", next(c for c in self.calls if c[:3] == ("gh", "release", "edit")))

    def test_verified_preview_draft_publishes_only_six_archives_after_download(self):
        assets = sorted(self.directory / release.archive_name(self.version, platform)
                        for platform in release.PLATFORMS)
        self.assertEqual(len(assets), 6)
        self.assertTrue(all(not a.name.endswith(".sha256") for a in assets))

        def command(*args):
            if args[0] == "git":
                return self.commit
            if args[:3] == ("gh", "release", "view"):
                return json.dumps({"isDraft": True, "assets": [{"name": p.name} for p in assets]})
            if args[:3] == ("gh", "release", "download"):
                destination = Path(args[args.index("--dir") + 1])
                for asset in assets:
                    (destination / asset.name).write_bytes(asset.read_bytes())
            return "mocked"

        verified = []
        self.publish(command, verify=lambda *a, **k: verified.append(a[5]) or self.metadata)
        self.assertEqual(sorted(verified), sorted(release.PLATFORMS))
        mutations = [args for args in self.calls if args[:2] == ("gh", "release")]
        self.assertEqual([args[2] for args in mutations], ["create", "view", "download", "edit"])
        self.assertEqual(len([a for a in mutations[0] if Path(a).parent == self.directory]), 6)
        self.assertTrue(all(not Path(a).name.endswith(".sha256") for a in mutations[0]
                            if Path(a).parent == self.directory))
        self.assertIn("--draft", mutations[0])
        self.assertIn("--verify-tag", mutations[0])
        self.assertIn("--prerelease=true", mutations[-1])
        self.assertIn("--latest=false", mutations[-1])


if __name__ == "__main__":
    unittest.main()
