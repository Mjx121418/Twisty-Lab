#!/usr/bin/env python3
"""Check reference packages; rewrite packages and semantic pins only when requested."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--write', action='store_true')
parser.add_argument('--update-fixtures', action='store_true')
args = parser.parse_args()
if args.update_fixtures and not args.write:
    parser.error('--update-fixtures requires --write')
root = Path(__file__).resolve().parent.parent
digests = {}
for name in ['cube3', 'bandaged']:
    package = root / 'packages' / name
    result = json.loads(subprocess.check_output([
        str(root / 'build/native/twisty'), 'compile', '--definition', str(package / 'source.json'), '--json'
    ], text=True))
    if result['status'] != 'Compiled':
        raise SystemExit(result)
    definition = result['definition']
    digests[name] = definition['definitionDigest']
    documents = {'definition.json': definition}
    for kind in ['cube-euclidean', 'cube-port-diagram']:
        documents[f'{kind}.json'] = {
            'schemaVersion': 1, 'id': f'{name}-{kind}-v1', 'kind': kind,
            'compatibleDefinitionDigest': definition['definitionDigest'],
            'requiredCapabilities': ['triangle-meshes', 'rigid-transforms']
        }
    for filename, document in documents.items():
        path = package / filename
        if args.write:
            path.write_text(json.dumps(document, indent=2, sort_keys=True) + '\n')
        elif json.loads(path.read_text()) != document:
            raise SystemExit(f'{path.relative_to(root)} is stale; review the change and run this script with --write.')
    print(f'{name}: {definition["definitionDigest"]}')
subprocess.run(['python3', str(root / 'scripts/helicopter_model.py')], check=True)
digests['helicopter'] = json.loads((root / 'packages/helicopter/definition.json').read_text())['definitionDigest']
fixture_path = root / 'tests/fixtures/core.json'
fixtures = json.loads(fixture_path.read_text())
for case in fixtures['cases']:
    name = Path(case['source']).parent.name
    if args.update_fixtures:
        case['definitionDigest'] = digests[name]
    elif case['definitionDigest'] != digests[name]:
        raise SystemExit(f'Fixture {case["name"]!r} has a different semantic pin; review it before updating.')
if args.update_fixtures:
    fixture_path.write_text(json.dumps(fixtures, indent=2) + '\n')
