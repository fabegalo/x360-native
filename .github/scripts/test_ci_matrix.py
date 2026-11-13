"""CI scheduling contracts, including the complete native release matrix."""

import ast
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import ci_matrix
import release


class PackageMatrixTests(unittest.TestCase):
    def test_native_pr_and_main_watch_the_same_inputs(self):
        workflow = (Path(__file__).resolve().parents[1] / "workflows/native-tests.yml").read_text(encoding="utf-8")
        triggers = workflow.split("  workflow_dispatch:", 1)[0]
        pr, push = triggers.split("  push:", 1)

        def paths(section):
            return {ast.literal_eval(line.strip()[2:]) for line in section.splitlines()
                    if line.startswith("      - ")}

        self.assertEqual(paths(pr), paths(push))
        self.assertTrue({"CMakeLists.txt", "deps.lock.json", "patches/**",
                         "external/rexglue-sdk", "tests/CMakeLists.txt"} <= paths(push))

    def test_github_output_is_a_single_json_matrix_and_preserves_other_outputs(self):
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch) / "outputs"
            output.write_text("version=1.2.3\n", encoding="utf-8")
            subprocess.run([sys.executable, "-B", str(Path(ci_matrix.__file__))],
                           env={**os.environ, "GITHUB_EVENT_NAME": "push",
                                "GITHUB_REF": "refs/tags/v1.2.3", "GITHUB_OUTPUT": str(output)},
                           check=True, timeout=10)
            lines = output.read_text(encoding="utf-8").splitlines()
            self.assertEqual(len(lines), 2)
            self.assertEqual(lines[0], "version=1.2.3")
            self.assertTrue(lines[1].startswith("matrix="))
            self.assertEqual(json.loads(lines[1][7:]),
                             ci_matrix.package_matrix("push", "refs/tags/v1.2.3"))

    def test_daily_covers_each_os_on_a_native_runner(self):
        for event, ref in (("push", "refs/heads/main"),
                           ("pull_request", "refs/pull/12/merge")):
            with self.subTest(event=event):
                rows = ci_matrix.package_matrix(event, ref)["include"]
                self.assertEqual({row["platform"] for row in rows},
                                 {"windows-x64", "linux-x64", "macos-arm64"})
                self.assertEqual(len(rows), 3)

    def test_tags_and_manual_runs_cover_every_publish_platform(self):
        for event, ref in (("push", "refs/tags/v1.2.3"),
                           ("workflow_dispatch", "refs/heads/main"),
                           ("workflow_dispatch", "refs/heads/test-ci")):
            with self.subTest(event=event, ref=ref):
                rows = ci_matrix.package_matrix(event, ref)["include"]
                self.assertEqual({row["platform"] for row in rows}, set(release.PLATFORMS))
                self.assertEqual(len(rows), 6)
                self.assertEqual({row["platform"]: row["runner"] for row in rows}, {
                    "windows-x64": "windows-2022", "windows-arm64": "windows-11-arm",
                    "linux-x64": "ubuntu-24.04", "linux-arm64": "ubuntu-24.04-arm",
                    "macos-arm64": "macos-15", "macos-x64": "macos-15-intel",
                })
                for row in rows:
                    self.assertEqual("." + row["archive"], release.PLATFORMS[row["platform"]]["archive"])


if __name__ == "__main__":
    unittest.main()
