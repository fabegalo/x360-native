"""Verify the exact fixed16 resources consumed by both renderer backends."""
from pathlib import Path
import hashlib
import json
import re
import struct
import subprocess
import argparse

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk-root', type=Path, default=root / 'external/rexglue-sdk')
args = parser.parse_args()
shaders = args.sdk_root / 'src/graphics/shaders'
sources = shaders / 'resolve_sources'
manifest = json.loads((sources / 'fixed16_generated.json').read_text())
sha = lambda data: hashlib.sha256(data).hexdigest()
assert len(manifest['resources']) == 20
for name, expected in manifest['sources'].items():
    assert sha((sources / name).read_bytes()) == expected, name
for name, record in manifest['resources'].items():
    text = (shaders / name).read_text()
    body = re.search(r'\[\]\s*=\s*\{(.*?)\};', text, re.S)[1]
    values = [int(t, 16) for t in re.findall(r'0x[0-9a-fA-F]+', body)]
    spirv = name.startswith('vulkan_spirv/')
    binary = struct.pack('<' + 'I' * len(values), *values) if spirv else bytes(values)
    assert binary[:4] == (b'\x03\x02\x23\x07' if spirv else b'DXBC'), name
    assert len(binary) == record['bytes'], name
    assert sha(binary) == record['binary_sha256'], name
    # The old approximate signed16 source scale must not survive compilation.
    words = struct.unpack('<' + 'I' * (len(binary) // 4), binary)
    assert 0x3A800100 not in words, name
    if 'ir_input_sha256' in record:
        original_path = 'src/graphics/shaders/' + name.replace('fixed16', 'full')
        original_text = subprocess.check_output(['git', '-C', str(shaders.parents[2]),
            'show', manifest['tools']['ir_input_commit'] + ':' + original_path], text=True)
        body = re.search(r'\[\]\s*=\s*\{(.*?)\};', original_text, re.S)[1]
        values = [int(t, 16 if t.startswith('0x') else 10)
                  for t in re.findall(r'0x[0-9a-fA-F]+|\b[0-9]+\b', body)]
        original = struct.pack('<' + 'I' * len(values), *values) if spirv else bytes(values)
        assert sha(original) == record['ir_input_sha256'], name
        original_words = struct.unpack('<' + 'I' * (len(original) // 4), original)
        expected = list(original_words)
        for index, value in record['ir_edits']:
            expected[index] = value
        assert len(words) == len(expected), name
        if not spirv:
            expected[1:5] = words[1:5]  # validated by real D3D12 pipeline creation
        assert tuple(expected) == words, name
print('20/20 fixed16 full resolve resources: hashes, source provenance and old-scale exclusion passed')
