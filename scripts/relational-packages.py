#!/usr/bin/env python3
"""Reproduce the migrated cube ASTs and their separate authoring bindings."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--native', default='build/native/twisty_relational')
parser.add_argument('--write', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
executable = root / args.native
session_executable = executable.parent / 'twisty'
if args.write:
    # Rebinding a geometric asset requires passing the abstract parity gate.
    subprocess.run([str(executable.parent / 'cube_relational_tests')], check=True)
for package, flag in [('cube3', '--cube-module'), ('bandaged', '--bandaged-module')]:
    module = json.loads(subprocess.check_output([str(executable), flag], text=True))
    bundle = dict(kind='relational-package', formatVersion=1, source=module['source'], catalog=module['catalog'])
    documents = {'relational.pzl.json': module['source'], 'relational-bindings.json': module['catalog'],
                 'relational-package.json': bundle}
    with tempfile.TemporaryDirectory(prefix='twisty-relational-') as directory:
        source_file = Path(directory) / 'package.json'
        source_file.write_text(json.dumps(bundle))
        result = json.loads(subprocess.check_output([str(session_executable), 'compile', '--definition', str(source_file), '--json'], text=True))
        if result['status'] != 'Compiled':
            raise SystemExit(result)
    for kind in ['cube-euclidean', 'cube-port-diagram', 'cube-spherical']:
        realization = json.loads((root / 'packages' / package / f'{kind}.json').read_text())
        realization['compatibleDefinitionDigest'] = result['definition']['definitionDigest']
        documents[f'relational-{kind}.json'] = realization
    for filename, content in documents.items():
        path = root / 'packages' / package / filename
        if args.write:
            path.write_text(json.dumps(content, indent=2, sort_keys=True) + '\n')
        elif not path.exists() or json.loads(path.read_text()) != content:
            raise SystemExit(f'{path.relative_to(root)} is stale; regenerate with --write after review.')
    print(f'{package}: relational source, bindings, package and geometric pins agree with the portable kernel')
