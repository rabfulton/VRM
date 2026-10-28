#!/usr/bin/env python3
"""Install a built checkout; DESTDIR staging never touches user configuration."""
from __future__ import annotations

import argparse
import compileall
import json
from pathlib import Path
import shutil
import subprocess
import sys
import sysconfig
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', default='/usr/local')
    parser.add_argument('--destdir', default='')
    parser.add_argument('--license-name', default='domovushka')
    args = parser.parse_args()
    prefix = Path(args.prefix)
    if not prefix.is_absolute():
        parser.error('--prefix must be absolute')
    if not args.license_name or '/' in args.license_name or args.license_name in ('.', '..'):
        parser.error('--license-name must be a package name')
    stage = Path(args.destdir).absolute() if args.destdir else Path('/')

    def staged(path):
        return stage / Path(path).relative_to('/')

    def directory(path):
        if path.is_dir():
            return
        directory(path.parent)
        path.mkdir()
        path.chmod(0o755)

    def copy(source, destination, mode=0o644):
        destination = staged(destination)
        directory(destination.parent)
        shutil.copyfile(source, destination)
        destination.chmod(mode)

    def write(text, destination, mode=0o644):
        destination = staged(destination)
        directory(destination.parent)
        destination.write_text(text)
        destination.chmod(mode)

    purelib = Path(sysconfig.get_path('purelib', vars={'base': str(prefix), 'platbase': str(prefix)}))
    module = purelib / 'vrm_overlay'
    data = prefix / 'share/domovushka'
    copy(ROOT / 'vrm-overlay', prefix / 'bin/vrm-overlay', 0o755)
    interpreter = Path(sys.base_prefix) / 'bin/python3'
    write(f'#!{interpreter}\nfrom vrm_overlay import gui\nraise SystemExit(gui.main())\n',
          prefix / 'bin/domovushka', 0o755)
    write(f'#!{interpreter}\nfrom vrm_overlay import mcp_server\nraise SystemExit(mcp_server.main())\n',
          prefix / 'bin/domovushka-mcp', 0o755)
    for source in (ROOT / 'vrm_overlay').glob('*.py'):
        copy(source, module / source.name)
    for folder, suffixes in [('qml', {'.qml'}), ('thumbnails', {'.png'})]:
        for source in (ROOT / 'vrm_overlay' / folder).rglob('*'):
            if source.is_file() and source.suffix in suffixes:
                copy(source, module / source.relative_to(ROOT / 'vrm_overlay'))
    write(f'# Generated at install time.\nBIN_DIR = {str(prefix / "bin")!r}\nDATA_DIR = {str(data)!r}\n', module / '_installed.py')

    starter = ROOT / 'assets/starter'
    manifest = json.loads((starter / 'manifest.json').read_text())
    files = set(manifest['artifacts'])
    files.update(p.name for p in starter.iterdir() if p.is_file())
    for name in sorted(files):
        source = starter / name
        if not source.resolve().is_relative_to(starter):
            raise ValueError(f'Invalid asset path: {name}')
        copy(source, data / 'assets/starter' / name)
    for source in (ROOT / 'docs').glob('*.md'):
        copy(source, prefix / 'share/doc/domovushka/docs' / source.name)
    copy(ROOT / 'README.md', prefix / 'share/doc/domovushka/README.md')
    copy(ROOT / 'packaging/domovushka.desktop', prefix / 'share/applications/domovushka.desktop')
    copy(ROOT / 'vrm_overlay/thumbnails/starter.png', prefix / 'share/pixmaps/domovushka.png')
    copy(ROOT / 'LICENSE', prefix / 'share/doc/domovushka/LICENSE')
    copy(ROOT / 'wayland.md', prefix / 'share/doc/domovushka/wayland.md')
    for name in ('PKGBUILD', '.SRCINFO'):
        source = ROOT / 'packaging/arch' / name
        if source.is_file():
            copy(source, prefix / 'share/doc/domovushka/packaging/arch' / name)

    licences = prefix / 'share/licenses' / args.license_name
    copy(ROOT / 'LICENSE', licences / 'LICENSE')
    for name, source in [('cglm.txt', 'deps/cglm/LICENSE'), ('ozz-animation.txt', 'deps/ozz-animation/LICENSE.md'),
                         ('starter-CC0.txt', 'assets/starter/CC0-1.0.txt')]:
        if name == 'ozz-animation.txt' and not (ROOT / source).exists():
            continue  # USE_OZZ=0 can be installed without an initialized submodule.
        copy(ROOT / source, licences / name)
    cjson = (ROOT / 'deps/cjson/cJSON.c').read_text().split('*/', 1)[0] + '*/\n'
    write(cjson, licences / 'cJSON.txt')
    tiny = (ROOT / 'deps/tinygltf/tiny_gltf.h').read_text()
    write(tiny.split('// Version:', 1)[0], licences / 'tinygltf.txt')
    write(tiny[tiny.index('/*\n   base64.cpp'):tiny.index('*/', tiny.index('/*\n   base64.cpp')) + 2] + '\n',
          licences / 'tinygltf-base64.txt')
    write(tiny[tiny.index('// Copyright (C) 2003'):tiny.index('namespace dlib {')], licences / 'tinygltf-dlib.txt')
    # JSON's source records its author/SPDX licence; include the complete MIT text.
    header = '\n'.join((ROOT / 'deps/tinygltf/json.hpp').read_text().splitlines()[:8])
    write(header + '\n\n' + cjson[cjson.index('  Permission'):], licences / 'nlohmann-json.txt')
    for name, source, marker in [('stb_image.txt', 'deps/stb/stb_image.h', 'This software is available under 2 licenses'),
                                 ('miniaudio.txt', 'deps/miniaudio.h', 'This software is available as a choice of the following licenses')]:
        text = (ROOT / source).read_text()
        write(text[text.rindex(marker):], licences / name)
    xml = ROOT / 'deps/wayland-protocols/wlr-layer-shell-unstable-v1.xml'
    write(ET.parse(xml).getroot().findtext('copyright').strip() + '\n', licences / 'wlr-layer-shell.txt')
    # Installed only when native Wayland was built; X11-only installs need no scanner data.
    if (ROOT / 'build/wayland-probe/xdg-shell-protocol.o').exists():
        protocols = subprocess.check_output(['pkg-config', '--variable=pkgdatadir', 'wayland-protocols'], text=True).strip()
        xml = Path(protocols) / 'stable/xdg-shell/xdg-shell.xml'
        write(ET.parse(xml).getroot().findtext('copyright').strip() + '\n', licences / 'xdg-shell.txt')
    if not compileall.compile_dir(str(staged(module)), quiet=1, stripdir=str(stage), prependdir='/', optimize=[0, 1, 2]):
        raise RuntimeError('Installed Python modules did not compile')
    for folder in staged(module).rglob('__pycache__'):
        folder.chmod(0o755)
        for bytecode in folder.glob('*.pyc'):
            bytecode.chmod(0o644)
    print(f'Installed domovushka under {staged(prefix)}')


if __name__ == '__main__':
    main()
