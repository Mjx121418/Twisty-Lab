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
# Reproduce current packages and geometry without the retired compiler.
# Geometry generators consume current authoring factories and bindings.
inventory = json.loads((root / 'scripts/puzzle-inventory.json').read_text())
for script in inventory['generators']:
    subprocess.run(['python3', str(root / 'scripts' / script)] + (['--write'] if args.write else []), check=True)
fixture_path = root / 'tests/fixtures/core.json'
fixtures = json.loads(fixture_path.read_text())
current_pins = {}
for case in fixtures['cases']:
    name = Path(case['source']).parent.name
    if name not in current_pins:
        current_pins[name] = json.loads(subprocess.check_output([str(root / 'build/native/twisty'), 'compile', '--definition', str(root / case['source']), '--json'], text=True))['definition']['definitionDigest']
    current = current_pins[name]
    if args.update_fixtures:
        case['definitionDigest'] = current
    elif case['definitionDigest'] != current:
        raise SystemExit(f'Fixture {case["name"]!r} has a different semantic pin; review it before updating.')
if args.update_fixtures:
    fixture_path.write_text(json.dumps(fixtures, indent=2) + '\n')
