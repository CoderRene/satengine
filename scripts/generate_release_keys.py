#!/usr/bin/env python3
"""Unobfuscated fallback provider. Contains no private obfuscation implementation."""
import argparse
import hashlib
import os
from pathlib import Path
import secrets
import tempfile

GROUPS = ('scripts', 'textures', 'audio', 'assets')

def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    fd, name = tempfile.mkstemp(dir=path.parent)
    try:
        os.chmod(name, 0o600)
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)

def generate(project, release, output):
    # The output directory is already isolated per project/build/release by CMake.
    output.mkdir(parents=True, exist_ok=True, mode=0o700)
    identity = hashlib.sha256((str(Path(project).resolve()) + '\0' + release).encode()).hexdigest()
    marker = output / 'release.identity'
    if marker.exists() and marker.read_text() != identity:
        raise RuntimeError('Release output belongs to a different release; use a fresh external directory')
    atomic_write(marker, identity.encode())
    keys = {}
    for group in GROUPS:
        path = output / (group + '.bin')
        if not path.exists():
            atomic_write(path, secrets.token_bytes(32))
        data = path.read_bytes()
        if len(data) != 32:
            raise RuntimeError('Invalid existing release key; refusing to replace it')
        keys[group] = data
    code = ['#include "GamePakKeys.hpp"', 'namespace sat {',
            'bool loadGamePakKey(std::string_view group, PakKey& result) {']
    for group, key in keys.items():
        code += ['    if (group == "' + group + '") {',
                 '        result = PakKey{{' + ','.join(map(str, key)) + '}};',
                 '        return true;', '    }']
    code += ['    return false;', '}', '}']
    atomic_write(output / 'GamePakKeys.cpp', ('\n'.join(code) + '\n').encode())

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--project', required=True)
    parser.add_argument('--release', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    project = Path(args.project).resolve()
    repository = Path(__file__).resolve().parent.parent
    if output == project or project in output.parents or output == repository or repository in output.parents:
        parser.error('Release key output must be outside the repository and game project')
    generate(args.project, args.release, output)
