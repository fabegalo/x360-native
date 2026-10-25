"""Build local reference kernels for the RGB565 investigation, never SDK resources.

The pinned Canary helpers already vendored by 0199/0200 retain the SDK's EDRAM
layout. Restore dynamic source selection instead of the gamma-only specialization.
--specialize reproduces the numerically correct but performance-rejected candidate.
No downloads or changes to the SDK, its manifests, or its shipped headers.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--specialize', action='store_true')
    parser.add_argument('--fxc', type=Path, default=Path(
        'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/fxc.exe'))
    parser.add_argument('--compiler', type=Path, default=root /
        '.local/build/win-amd64/tests/sdkcheck/g3/Release/x360n_resolve_shader_compiler.exe')
    parser.add_argument('--spirv-tools', type=Path, default=root / '.local/shader-tools/spirv/tools')
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    # Keep these experimental bytecodes and copied sources outside version control.
    if not output.is_relative_to(root / '.local'):
        parser.error('--output must be inside the ignored .local directory')
    sources = root / 'external/rexglue-sdk/src/graphics/shaders/resolve_sources'
    stage = output / 'sources'
    stage.mkdir(parents=True, exist_ok=True)
    sha = lambda data: hashlib.sha256(data).hexdigest()
    manifest = {'specialize': args.specialize, 'inputs': {}, 'resources': {},
                'lock_sha256': sha((root / 'deps.lock.json').read_bytes()),
                'generator_sha256': sha(Path(__file__).read_bytes().replace(b'\r\n', b'\n')),
                'fxc_sha256': sha(args.fxc.read_bytes())}
    for p in sorted(sources.glob('*.xesl*')):
        manifest['inputs'][p.name] = sha(p.read_bytes())
        shutil.copyfile(p, stage / p.name)
    p = stage / 'resolve.xesli'
    text = p.read_text()
    start = text.index('#ifdef XE_RESOLVE_FIXED16', text.index('XeResolveInfo XeResolveGetInfo'))
    end = text.index('#endif', start) + len('#endif')
    text = text[:start] + '''  resolve_info.edram_format = (edram_info >> 24u) & 15u;
  resolve_info.edram_format_ints_log2 = (edram_info >> 28u) & 1u;
  resolve_info.decode_pwl_gamma = false;
''' + text[end:]
    if args.specialize:
        for old, new in (
            ('resolve_info.dest_format = (dest_info >> 7u) & ((1u << 6u) - 1u);',
             'resolve_info.dest_format = 4u;'),
            ('resolve_info.dest_num_format = (dest_info >> 13u) & ((1u << 3u) - 1u);',
             'resolve_info.dest_num_format = 0u;')):
            assert text.count(old) == 1
            text = text.replace(old, new)
    p.write_text(text, newline='\n')
    manifest['adapted_resolve_sha256'] = sha(p.read_bytes())
    for name in ('glslang', 'spirv-tools', 'spirv-headers'):
        manifest[name] = subprocess.check_output(['git', '-C', str(root /
            'external/rexglue-sdk/thirdparty' / name), 'rev-parse', 'HEAD'], text=True).strip()
    manifest['compiler_source_sha256'] = sha((root /
        'scripts/shaders/compile_resolve_spirv.cpp').read_bytes().replace(b'\r\n', b'\n'))

    def run(command):
        subprocess.run(list(map(str, command)), check=True, timeout=120,
                       stdout=subprocess.DEVNULL)

    for bits in ((16,) if args.specialize else (8, 16)):
        for suffix in (('_scaled',) if args.specialize else ('', '_scaled')):
            name = f'resolve_full_{bits}bpp{suffix}_cs'
            source = stage / f'resolve_full_{bits}bpp{suffix}.cs.xesl'
            source.write_text(('#define XE_RESOLVE_RESOLUTION_SCALED 1\n' if suffix else '') +
                              f'#include "resolve_full_{bits}bpp.xesli"\n', newline='\n')
            dxbc, raw, spv = (stage / (name + ext) for ext in ('.dxbc', '.raw.spv', '.spv'))
            run([args.fxc, '/nologo', '/D', 'SHADING_LANGUAGE_HLSL_XE=1', '/T', 'cs_5_1',
                 '/E', 'main', '/O3', '/Gfp', '/Gis', '/Qstrip_reflect', '/Qstrip_debug',
                 '/Qstrip_priv', '/Fo', dxbc, source])
            run([args.compiler, source, raw])
            run([args.spirv_tools / 'spirv-opt.exe', '--target-env=vulkan1.0',
                 '-O', '-O', '--compact-ids', raw, '-o', spv])
            run([args.spirv_tools / 'spirv-val.exe', '--target-env', 'vulkan1.0', spv])
            for path in (dxbc, spv):
                data = path.read_bytes()
                dest = output / path.name
                if args.verify:
                    assert dest.read_bytes() == data, dest
                else:
                    dest.write_bytes(data)
                manifest['resources'][path.name] = {'sha256': sha(data), 'bytes': len(data)}
            print(name, flush=True)
    data = (json.dumps(manifest, indent=2) + '\n').encode()
    path = output / 'manifest.json'
    if args.verify:
        assert path.read_bytes() == data, path
    else:
        path.write_bytes(data)
    print('Reference resources verified' if args.verify else 'Local reference resources generated')


if __name__ == '__main__':
    main()
