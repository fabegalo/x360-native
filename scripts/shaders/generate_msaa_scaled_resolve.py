"""Regenerate only the private scaled MSAA RGB565 resources (DXBC and SPIR-V)."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_resolves import header, statistics


def main():
    root = Path(__file__).resolve().parents[2]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk-root', type=Path, default=root / 'external/rexglue-sdk')
    p.add_argument('--fxc', required=True, type=Path)
    p.add_argument('--spirv-compiler', required=True, type=Path)
    p.add_argument('--spirv-tools', required=True, type=Path)
    p.add_argument('--binary-dir', type=Path)
    p.add_argument('--verify', action='store_true')
    args = p.parse_args()
    shaders = args.sdk_root.resolve() / 'src/graphics/shaders'
    sources = shaders / 'resolve_sources'
    name = 'resolve_msaa_scaled_16bpp_cs'
    sha = lambda data: hashlib.sha256(data).hexdigest()
    inputs = ['resolve_msaa_scaled_16bpp.cs.xesl',
              'resolve_msaa_scaled_16bpp_vulkan.cs.xesl', 'resolve_msaa_scaled_address.xesli',
              'resolve_full_16bpp.xesli', 'resolve.xesli', 'edram.xesli',
              'pixel_formats.xesli', 'endian.xesli', 'xesl.xesli', 'texture_address.xesli']
    manifest = {'sources': {n: sha((sources / n).read_bytes()) for n in inputs},
                'tools': {'fxc_sha256': sha(args.fxc.read_bytes()),
                          'generator_source_sha256': sha(Path(__file__).read_bytes().replace(b'\r\n', b'\n')),
                          'common_generator_source_sha256': sha((root / 'scripts/shaders/generate_resolves.py').read_bytes().replace(b'\r\n', b'\n')),
                          'spirv_compiler_source_sha256': sha((root / 'scripts/shaders/compile_resolve_spirv.cpp').read_bytes().replace(b'\r\n', b'\n')),
                          'spirv_target': 'vulkan1.0',
                          'fxc_flags': '/T cs_5_1 /O3 /Gfp /Gis /Qstrip_reflect /Qstrip_debug /Qstrip_priv'},
                'resources': {}}
    for tool in ('glslang', 'spirv-tools', 'spirv-headers'):
        manifest['tools'][tool] = subprocess.check_output(['git', '-C',
            str(args.sdk_root / 'thirdparty' / tool), 'rev-parse', 'HEAD'], text=True).strip()
    temp_root = root / '.local/tmp'
    temp_root.mkdir(parents=True, exist_ok=True)
    generated = {}

    def run(command):
        subprocess.run(list(map(str, command)), check=True, timeout=120,
                       stdout=subprocess.DEVNULL)

    with tempfile.TemporaryDirectory(prefix='scaled-resolve-', dir=temp_root) as directory:
        temp = Path(directory).resolve()
        assert temp.is_relative_to(temp_root.resolve())
        dxbc, spv = (temp / (name + ext) for ext in ('.dxbc', '.spv'))
        source = sources / 'resolve_msaa_scaled_16bpp.cs.xesl'
        run([args.fxc, '/nologo', '/D', 'SHADING_LANGUAGE_HLSL_XE=1', '/T', 'cs_5_1',
             '/E', 'main', '/O3', '/Gfp', '/Gis', '/Qstrip_reflect', '/Qstrip_debug',
             '/Qstrip_priv', '/Fo', dxbc, source])
        raw = temp / 'raw.spv'
        run([args.spirv_compiler, sources / 'resolve_msaa_scaled_16bpp_vulkan.cs.xesl', raw])
        run([args.spirv_tools / 'spirv-opt.exe', '--target-env=vulkan1.0', '-O', '-O',
             '--compact-ids', raw, '-o', spv])
        run([args.spirv_tools / 'spirv-val.exe', '--target-env', 'vulkan1.0', spv])
        for path, directory, spirv, binary_name in ((dxbc, 'bytecode/d3d12_5_1', False, name),
                                        (spv, 'vulkan_spirv', True, name)):
            data = path.read_bytes()
            relative = f'{directory}/{binary_name}.h'
            generated[shaders / relative] = header(data, binary_name, spirv).replace(
                b'generate_resolves.py', b'generate_msaa_scaled_resolve.py')
            manifest['resources'][relative] = {'binary_sha256': sha(data),
                'bytes': len(data), **statistics(data, spirv)}
            if args.binary_dir:
                args.binary_dir.mkdir(parents=True, exist_ok=True)
                (args.binary_dir / (binary_name + path.suffix)).write_bytes(data)
    generated[sources / 'msaa_scaled_generated.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    for path, data in generated.items():
        if args.verify:
            assert path.read_bytes() == data, path
        else:
            path.write_bytes(data)
    print('Scaled MSAA resources verified' if args.verify else '2 scaled MSAA resources generated')


if __name__ == '__main__':
    main()
