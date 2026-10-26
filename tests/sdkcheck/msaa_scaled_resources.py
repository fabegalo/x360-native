"""Verify resources and selection of the private scaled MSAA resolve pipeline."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk-root', type=Path, default=root / 'external/rexglue-sdk')
args = p.parse_args()
shaders = args.sdk_root / 'src/graphics/shaders'
sources = shaders / 'resolve_sources'
manifest = json.loads((sources / 'msaa_scaled_generated.json').read_text())
sha = lambda data: hashlib.sha256(data).hexdigest()
assert len(manifest['resources']) == 2
for name, digest in manifest['sources'].items():
    assert sha((sources / name).read_bytes()) == digest, name
for name, record in manifest['resources'].items():
    body = re.search(r'\[\]\s*=\s*\{(.*?)\};', (shaders / name).read_text(), re.S)[1]
    values = [int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', body)]
    data = struct.pack('<' + 'I' * len(values), *values) if name.startswith('vulkan_spirv/') else bytes(values)
    assert len(data) == record['bytes'] and sha(data) == record['binary_sha256'], name
for api in ('d3d12', 'vulkan'):
    text = (args.sdk_root / f'src/graphics/{api}/render_target_cache.cpp').read_text()
    assert 'UseScaledRgb565Resolve(' in text
    assert ('i == kResolveRgb565ShaderIndex && !draw_resolution_scaled' in text or
            'i >= kResolveRgb565ShaderIndex && !draw_resolution_scaled' in text)
    assert 'shaders::resolve_msaa_scaled_16bpp_cs' in text
    assert 'scaled_rgb565 ? kResolveRgb565ShaderIndex' in text
print('2/2 scaled MSAA resources: sources, hashes and private selection verified')
