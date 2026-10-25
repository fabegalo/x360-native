"""Extract original resolve binaries from the pinned SDK for GPU comparisons.

Reads existing Git objects only. No network access or shader compiler is needed.
Use with gamma_resolve_gpu_checks --baseline-dir <output>.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    sdk_info = json.loads((root / "deps.lock.json").read_text())["rexglue_sdk"]
    sdk = root / sdk_info["path"]
    args.output.mkdir(parents=True, exist_ok=True)
    for bpp in (8, 16, 32, 64, 128):
        for suffix in ("", "_scaled"):
            name = f"resolve_full_{bpp}bpp{suffix}_cs"
            for directory, extension in (("bytecode/d3d12_5_1", ".dxbc"),
                                         ("vulkan_spirv", ".spv")):
                relative = f"src/graphics/shaders/{directory}/{name}.h"
                source = subprocess.check_output(
                    ["git", "-C", str(sdk), "show", sdk_info["commit"] + ":" + relative],
                    text=True)
                declaration = re.search(r"\b" + name + r"\[\]\s*=\s*\{(.*?)\};",
                                        source, re.S)
                if not declaration:
                    raise RuntimeError(f"missing original shader array: {relative}")
                values = [int(token, 16 if token.startswith("0x") else 10) for token in
                          re.findall(r"0x[0-9a-fA-F]+|\b[0-9]+\b", declaration[1])]
                data = (bytes(values) if extension == ".dxbc" else
                        struct.pack("<" + "I" * len(values), *values))
                expected_magic = b"DXBC" if extension == ".dxbc" else b"\x03\x02\x23\x07"
                if len(data) < 20 or data[:4] != expected_magic:
                    raise RuntimeError(f"invalid original resource: {relative}")
                (args.output / (name + extension)).write_bytes(data)
    print("20 original SDK resources extracted from", sdk_info["commit"])


if __name__ == "__main__":
    main()
