"""Create private DDS derivatives from locally dumped textures; bundles no assets.

SPDX-License-Identifier: MIT. Requires Pillow and caller-supplied texconv.
Optional opaque 4x upscaling uses caller-supplied Real-ESRGAN NCNN Vulkan tools.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from PIL import Image


def command(args):
    subprocess.run([str(a) for a in args], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dumps', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='New pack directory')
    parser.add_argument('--texconv', type=Path, required=True)
    parser.add_argument('--method', choices=['lanczos', 'realesrgan'], default='lanczos')
    parser.add_argument('--upscaler', type=Path)
    parser.add_argument('--models', type=Path)
    parser.add_argument('--include-alpha', action='store_true', help='Use Lanczos for alpha textures')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Use a new output directory; existing packs are never overwritten')
    if args.method == 'realesrgan' and not (args.upscaler and args.models):
        parser.error('Real-ESRGAN requires --upscaler and --models')
    for executable in [args.texconv] + ([args.upscaler] if args.method == 'realesrgan' else []):
        if not executable.is_file():
            parser.error('Caller-supplied tool executable missing')
    args.output.mkdir(parents=True, exist_ok=False)
    stage = args.output / 'intermediate'
    stage.mkdir()
    records = []
    for source in sorted(args.dumps.glob('*.dds')):
        if not re.fullmatch(r'[0-9a-f]{64}', source.stem) or source.stat().st_size > 128 * 1024 * 1024:
            continue
        with Image.open(source) as opened:
            if min(opened.size) < 16 or max(opened.size) > 2048:
                continue
            image = opened.convert('RGBA')
        opaque = image.getchannel('A').getextrema() == (255, 255)
        if not opaque and not args.include_alpha:
            continue
        scale = 4
        target = stage / (source.stem + '.png')
        method = 'lanczos'
        if args.method == 'realesrgan' and opaque:
            input_png = stage / (source.stem + '-source.png')
            image.convert('RGB').save(input_png)
            command([args.upscaler.resolve(), '-i', input_png.resolve(), '-o', target.resolve(),
                     '-m', args.models.resolve(), '-n', 'realesrgan-x4plus', '-s', '4',
                     '-t', '128', '-f', 'png', '-j', '1:1:1'])
            method = 'realesrgan-x4plus'
        else:
            image.resize((image.width * scale, image.height * scale), Image.Resampling.LANCZOS).save(target)
        with Image.open(target) as check:
            if check.size != (image.width * scale, image.height * scale):
                raise ValueError('Unexpected upscaler output dimensions')
        # Full mip chain, DX10 header. UNORM matches the local linear-color dumps.
        # sRGB inputs need a format-aware path and are excluded below.
        raw = source.read_bytes()
        if len(raw) < 148 or raw[84:88] != b'DX10':
            raise ValueError('Expected DX10 DDS from NativeTextures')
        fmt = int.from_bytes(raw[128:132], 'little')
        if fmt not in [28, 87, 71, 74, 77, 98]:
            continue
        output_format = 'BC7_UNORM' if opaque else 'R8G8B8A8_UNORM'
        command([args.texconv.resolve(), '-nologo', '-dx10', '-l', '-ft', 'dds',
                 '-f', output_format, '-m', '0', '-wrap', '-o', args.output.resolve(), target.resolve()])
        output = args.output / (source.stem + '.dds')
        if not output.is_file():
            raise ValueError('DDS encoder did not produce the expected file')
        records.append(dict(hash=source.stem, method=method,
                            source_sha256=hashlib.sha256(raw).hexdigest(),
                            output_sha256=hashlib.sha256(output.read_bytes()).hexdigest()))
    (args.output / 'manifest.json').write_text(json.dumps(dict(
        notice='Private derivatives of caller-supplied dumps; no redistribution rights granted.',
        records=records), indent=2), encoding='utf-8')
    print(f'Created {len(records)} private replacements. Review atlas/alpha quality before use.')


if __name__ == '__main__':
    main()
