import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "sdk_patches", Path(__file__).resolve().parents[2] / "scripts/setup/apply-sdk-patches.py"
)
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)


class LockedLineEndings(unittest.TestCase):
    def test_only_exact_locked_bytes_are_normalized(self):
        with tempfile.TemporaryDirectory(prefix="x360n-patch-eol-") as folder:
            previous = patches.SDK
            patches.SDK = Path(folder)
            try:
                expected = b"first\nsecond\n"
                path = patches.SDK / "source.h"
                entry = {"base_files": {"source.h": hashlib.sha256(expected).hexdigest()}}
                path.write_bytes(b"first\r\nsecond\r\n")
                patches.normalize_line_endings([entry])
                self.assertEqual(path.read_bytes(), expected)
                changed = b"concurrent edit\r\nsecond\r\n"
                path.write_bytes(changed)
                patches.normalize_line_endings([entry])
                self.assertEqual(path.read_bytes(), changed)
                binary = b"\x00\xff\r\n\x80"
                path.write_bytes(binary)
                entry["base_files"]["source.h"] = hashlib.sha256(binary).hexdigest()
                patches.normalize_line_endings([entry])
                self.assertEqual(path.read_bytes(), binary)
            finally:
                patches.SDK = previous


if __name__ == "__main__":
    unittest.main()
