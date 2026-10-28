#!/usr/bin/env python3
"""Build the current working tree with the Arch PKGBUILD, without a Git push."""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def git(*args, cwd=ROOT):
    return subprocess.check_output(['git', *args], cwd=cwd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/packages')
    parser.add_argument('makepkg_args', nargs=argparse.REMAINDER, help='Arguments after -- are forwarded to makepkg')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    version = f'r{git("rev-list", "--count", "HEAD").decode().strip()}.{git("rev-parse", "--short=7", "HEAD").decode().strip()}.local'
    entries = git('ls-files', '-z', '--cached', '--others', '--exclude-standard').split(b'\0')
    submodule = ROOT / 'deps/ozz-animation'
    if not (submodule / 'CMakeLists.txt').is_file():
        parser.error('Initialize ozz first: git submodule update --init --recursive')
    repository = Path(git('rev-parse', '--show-toplevel', cwd=submodule).decode().strip())
    if repository.resolve() == submodule.resolve():
        ozz_files = [Path(os.fsdecode(name)) for name in git('ls-files', '-z', cwd=submodule).split(b'\0') if name]
    else:
        # Source archives/existing development trees may have unpacked ozz data
        # without its Git metadata. Do not accidentally query the parent index.
        ozz_files = [path.relative_to(submodule) for path in submodule.rglob('*')
                     if path.is_file() and not {'.git', '__pycache__', 'build'}.intersection(path.relative_to(submodule).parts)]
    entries += [os.fsencode(Path('deps/ozz-animation') / name) for name in ozz_files]
    # Match makepkg's source layout, including uncommitted source changes but no
    # ignored models/settings, build output or submodule Git metadata.
    with tempfile.TemporaryDirectory(prefix='domovushka-arch-') as temporary:
        work = Path(temporary)
        archive = work / 'domovushka.tar.gz'
        with tarfile.open(archive, 'w:gz') as tar:
            for entry in sorted(set(entries)):
                if not entry:
                    continue
                relative = Path(os.fsdecode(entry))
                source = ROOT / relative
                if source.is_relative_to(output):
                    continue
                if source.is_file() and '.git' not in relative.parts:
                    tar.add(source, arcname=str(Path('domovushka') / relative), recursive=False)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        recipe = (ROOT / 'packaging/arch/PKGBUILD').read_text()
        recipe += (f'\n# Local working-tree snapshot; upstream build/check/package functions are unchanged.\n'
                   f'source=("domovushka.tar.gz")\nsha256sums=("{digest}")\n'
                   f'pkgver() {{ printf "%s" "{version}"; }}\nprepare() {{ :; }}\n')
        (work / 'PKGBUILD').write_text(recipe)
        forwarded = args.makepkg_args
        if forwarded[:1] == ['--']:
            forwarded = forwarded[1:]
        environment = dict(os.environ, PKGDEST=str(output))
        result = subprocess.run(['makepkg', '--cleanbuild', '--force', *forwarded], cwd=work, env=environment)
        if result.returncode:
            raise SystemExit(result.returncode)
    print(f'Packages are in {output}')


if __name__ == '__main__':
    main()
