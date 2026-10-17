"""Synthetic release-gate tests; no original Xbox content or native execution."""

import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import release


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.version = "0.1.0-preview.21"
        self.commit = "a" * 40
        self.ref = "refs/tags/v" + self.version
        self.name = f"x360-native-{self.version}-windows-x64"
        self.archive = self.directory / (self.name + ".zip")
        pe = bytearray(128)
        pe[:2] = b"MZ"
        pe[60:64] = (64).to_bytes(4, "little")
        pe[64:70] = b"PE\0\0\x64\x86"
        self.files = {exe: bytes(pe) for exe in release.EXECUTABLES}
        self.files.update({"LICENSE": b"synthetic license", "THIRD_PARTY_NOTICES.md": b"synthetic notice"})
        patch_bytes = b"synthetic patch fixture"
        patches = [{"file": "patches/rexglue-sdk/fixture.patch", "sha256": release.digest(patch_bytes)}]
        inventory = {"sdk_commit": "b" * 40, "components": [{"id": "fmt", "revision": "c" * 40}], "desktop_dependencies": ["fmt"]}
        tool = dict(id="ninja", version="1.13.1", sha256="d" * 64)
        provenance = dict(commit=self.commit, version=self.version, ref=self.ref, run_id="42",
                          tag="v" + self.version, llvm_archive_sha256="e" * 64, tools=[tool])
        self.metadata = dict(version=self.version, commit=self.commit, platform="windows-x64", configuration="Release",
                             desktop_only=True, sdk_commit=inventory["sdk_commit"], patches=patches,
                             dependencies={"fmt": "c" * 40}, provenance=provenance)
        self.files["licenses/inventory.json"] = json.dumps(inventory).encode()
        for notice in ("LICENSE", "THIRD_PARTY_NOTICES.md", "licenses/inventory.json"):
            self.files["app/workspace/" + notice] = self.files[notice]
        self.files["app/workspace/" + patches[0]["file"]] = patch_bytes
        self.files["app/workspace/deps.lock.json"] = json.dumps({"rexglue_sdk": {"commit": inventory["sdk_commit"], "patches": patches}, "llvm": {"sha256": "e" * 64}}).encode()
        self.files["app/workspace/scripts/setup/desktop-tools.lock.json"] = json.dumps({"tools": [tool]}).encode()

    def write_archive(self):
        def index(prefix, name):
            return "".join(f"{release.digest(data)}\t{path[len(prefix):]}\n" for path, data in sorted(self.files.items())
                           if path.startswith(prefix) and path != prefix + name).encode()
        self.files["app/workspace/payload.sha256"] = index("app/workspace/", "payload.sha256")
        self.metadata["payload_sha256"] = release.digest(self.files["app/workspace/payload.sha256"])
        self.files["package.json"] = json.dumps(self.metadata).encode()
        self.files["package.sha256"] = index("", "package.sha256")
        with zipfile.ZipFile(self.archive, "w") as archive:
            for name, data in self.files.items():
                archive.writestr(self.name + "/" + name, data)
        self.write_checksum()

    def write_checksum(self):
        Path(str(self.archive) + ".sha256").write_text(f"{release.file_digest(self.archive)}  {self.archive.name}\n")

    def verify(self):
        return release.verify(self.directory, self.version, self.commit, self.ref, "42")

    def test_complete_archive(self):
        self.write_archive()
        self.assertEqual(self.verify()["commit"], self.commit)

    def test_corrupt_download(self):
        self.write_archive()
        with self.archive.open("ab") as archive:
            archive.write(b"tampered")
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            self.verify()

    def test_unindexed_file(self):
        self.write_archive()
        with zipfile.ZipFile(self.archive, "a") as archive:
            archive.writestr(self.name + "/extra.txt", b"extra")
        self.write_checksum()
        with self.assertRaisesRegex(ValueError, "Unindexed"):
            self.verify()

    def test_incomplete_deliverables(self):
        self.write_archive()
        Path(str(self.archive) + ".sha256").unlink()
        with self.assertRaisesRegex(ValueError, "exactly"):
            self.verify()

    def test_self_consistent_forbidden_files(self):
        for name, data, reason in (("font.xtt", b"synthetic", "Forbidden"),
                                   ("foreign.dll", b"MZ", "Forbidden"),
                                   ("renamed.dat", b"XEX2synthetic", "Original"),
                                   ("renamed.dat", b"MZsynthetic", "Renamed"),
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
        with self.assertRaisesRegex(ValueError, "Duplicate ZIP"):
            self.verify()

    def test_missing_and_wrong_architecture(self):
        original = self.files.pop("app/x360n.exe")
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "Missing/invalid"):
            self.verify()
        wrong = bytearray(original)
        wrong[68:70] = b"\x4c\x01"
        self.files["app/x360n.exe"] = bytes(wrong)
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "not Windows x64"):
            self.verify()

    def test_wrong_build_provenance(self):
        for field, value, reason in (("commit", "wrong", "source/version"), ("configuration", "Debug", "Release"),
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

    def test_changed_patch_with_refreshed_indices(self):
        self.files["app/workspace/patches/rexglue-sdk/fixture.patch"] = b"changed patch"
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "patch hash"):
            self.verify()

    def test_versions_and_modes(self):
        self.assertEqual(release.release_identity("push", self.ref, "ignored"), self.version)
        self.assertEqual(release.release_identity("push", "refs/tags/v1.2.3", ""), "1.2.3")
        self.assertEqual(release.release_identity("workflow_dispatch", "refs/heads/main", "1.2.3-ci"), "1.2.3-ci")
        for event, ref, version in (("pull_request", self.ref, "1.2.3"), ("push", "refs/heads/main", "1.2.3"),
                                    ("workflow_dispatch", self.ref, "../../escape"), ("workflow_dispatch", self.ref, "v1.2.3")):
            with self.subTest(event=event, version=version), self.assertRaises(ValueError):
                release.release_identity(event, ref, version)

    def test_manual_tag_run_cannot_publish(self):
        with patch.dict("os.environ", GITHUB_EVENT_NAME="workflow_dispatch", GITHUB_REF=self.ref), patch.object(release, "command") as command:
            with self.assertRaisesRegex(ValueError, "Manual runs"):
                release.publish()
            command.assert_not_called()

    def test_missing_release_notes(self):
        with self.assertRaisesRegex(ValueError, "notes missing"):
            release.notes_for("999.999.999")

    def test_remote_annotated_tag(self):
        with patch.object(release, "command", side_effect=[json.dumps({"object": {"type": "tag", "sha": "d" * 40}}),
                                                             json.dumps({"object": {"type": "commit", "sha": self.commit}})]):
            self.assertEqual(release.remote_tag_commit("owner/repo", "v1.2.3"), self.commit)

    def test_publish_has_no_mutation_before_gate_success(self):
        env = dict(GITHUB_EVENT_NAME="push", GITHUB_REF=self.ref, GITHUB_SHA=self.commit, GITHUB_RUN_ID="42")
        with patch.dict("os.environ", env), patch.object(release, "command", return_value=self.commit) as command, \
                patch.object(release, "verify", side_effect=ValueError("Incomplete artifact")):
            with self.assertRaisesRegex(ValueError, "Incomplete"):
                release.publish()
            self.assertTrue(all(call.args[0] == "git" for call in command.call_args_list))

    def test_incomplete_draft_never_publishes(self):
        env = dict(GITHUB_EVENT_NAME="push", GITHUB_REF=self.ref, GITHUB_SHA=self.commit, GITHUB_RUN_ID="42", GITHUB_REPOSITORY="owner/repo")
        notes = self.directory / "notes.md"
        notes.write_text("# Test release\n\nSynthetic notes.\n")
        self.metadata["provenance"].update(repository="owner/repo", event="push", release_notes_sha256=release.file_digest(notes))
        with patch.dict("os.environ", env), patch.object(release, "command", side_effect=[self.commit, self.commit, "draft-url", json.dumps({"isDraft": True, "assets": []})]) as command, \
                patch.object(release, "verify", return_value=self.metadata), patch.object(release, "notes_for", return_value=(notes, "Test release")), \
                patch.object(release, "remote_tag_commit", return_value=self.commit), patch.object(Path, "iterdir", return_value=iter([self.archive])), \
                patch.object(Path, "is_file", return_value=True):
            with self.assertRaisesRegex(ValueError, "Draft assets incomplete"):
                release.publish()
            self.assertFalse(any(call.args[:3] == ("gh", "release", "edit") for call in command.call_args_list))

    def test_verified_preview_draft_publishes_after_download(self):
        self.write_archive()
        assets = [self.archive, Path(str(self.archive) + ".sha256")]
        notes = self.directory / "notes.md"
        notes.write_text("# Test release\n\nSynthetic notes.\n")
        self.metadata["provenance"].update(repository="owner/repo", event="push", release_notes_sha256=release.file_digest(notes))
        env = dict(GITHUB_EVENT_NAME="push", GITHUB_REF=self.ref, GITHUB_SHA=self.commit, GITHUB_RUN_ID="42", GITHUB_REPOSITORY="owner/repo")

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

        with patch.dict("os.environ", env), patch.object(release, "command", side_effect=command) as mocked, \
                patch.object(release, "verify", return_value=self.metadata), patch.object(release, "notes_for", return_value=(notes, "Test release")), \
                patch.object(release, "remote_tag_commit", return_value=self.commit), patch.object(Path, "iterdir", side_effect=lambda: iter(assets)):
            release.publish()
            mutations = [call.args for call in mocked.call_args_list if call.args[:2] == ("gh", "release")]
            self.assertEqual([args[2] for args in mutations], ["create", "view", "download", "edit"])
            self.assertIn("--draft", mutations[0])
            self.assertIn("--verify-tag", mutations[0])
            self.assertIn("--prerelease=true", mutations[-1])
            self.assertIn("--latest=false", mutations[-1])


if __name__ == "__main__":
    unittest.main()
