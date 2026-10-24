#!/usr/bin/env python3
"""Reproduce the migrated cube ASTs and their separate authoring bindings."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--native', default='build/native/twisty_language')
parser.add_argument('--write', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
executable = root / args.native
session_executable = executable.parent / 'twisty'
for package, flag in [('cube3', '--cube-module'), ('bandaged', '--bandaged-module')]:
    module = json.loads(subprocess.check_output([str(executable), flag], text=True))
    bundle = dict(kind='relational-package', formatVersion=2, source=module['source'], catalog=module['catalog'])
    documents = {'puzzle.pzl.json': module['source'], 'bindings.json': module['catalog'],
                 'package.json': bundle}
    with tempfile.TemporaryDirectory(prefix='twisty-language-') as directory:
        source_file = Path(directory) / 'package.json'
        source_file.write_text(json.dumps(bundle))
        result = json.loads(subprocess.check_output([str(session_executable), 'compile', '--definition', str(source_file), '--json'], text=True))
        if result['status'] != 'Compiled':
            raise SystemExit(result)
    for kind in ['cube-euclidean', 'cube-port-diagram', 'cube-spherical']:
        realization = dict(schemaVersion=1, id=f'{package}-{kind}-v1', kind=kind,
                           requiredCapabilities=['triangle-meshes', 'rigid-transforms'],
                           compatibleDefinitionDigest=result['definition']['definitionDigest'])
        if kind == 'cube-spherical':
            realization.update(angularSegments=64, diskAngleDegrees=70, radialSegments=5,
                               radius=2.05, portInset=0.04, surfaceLift=0.02,
                               fidelity='spherical-surface-model')
        documents[f'{kind}.json'] = realization
    for filename, content in documents.items():
        path = root / 'packages' / package / filename
        if args.write:
            path.write_text(json.dumps(content, indent=2, sort_keys=True) + '\n')
        elif not path.exists() or json.loads(path.read_text()) != content:
            raise SystemExit(f'{path.relative_to(root)} is stale; regenerate with --write after review.')
    print(f'{package}: relational source, bindings, package and geometric pins agree with the portable kernel')
