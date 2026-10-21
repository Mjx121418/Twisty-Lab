#!/usr/bin/env python3
"""Generate portable Gear language modules from the current authoring factory."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    native = ROOT / 'build/native'
    for name in ['gear', 'gear-extreme', 'gear-plain']:
        directory = ROOT / 'packages' / name
        wrapper = json.loads((directory / 'source.json').read_text())
        with tempfile.TemporaryDirectory(prefix='twisty-gear-') as temporary:
            path = Path(temporary) / 'source.json'
            path.write_text(json.dumps(wrapper))
            module = json.loads(subprocess.check_output([str(native / 'twisty_language'), str(path), '--gear-module'], text=True))
            compiled = json.loads(subprocess.check_output([str(native / 'twisty'), 'compile', '--definition', str(path), '--json'], text=True))
            if compiled['status'] != 'Compiled':
                raise SystemExit(compiled)
        digest = compiled['definition']['definitionDigest']
        documents = {'source.json': wrapper, 'puzzle.pzl.json': module['source'],
                     'bindings.json': module['catalog'],
                     'package.json': dict(kind='relational-package', formatVersion=1, **module),
                     'manifest.json': dict(puzzleId=wrapper['puzzleId'], definitionDigest=digest)}
        for filename, content in documents.items():
            path = directory / filename
            if args.write:
                path.write_text(json.dumps(content, separators=(',', ':'), sort_keys=True) + '\n')
            elif not path.exists() or json.loads(path.read_text()) != content:
                raise SystemExit(f'{path.relative_to(ROOT)} is stale; review before regenerating.')
        print(f'{name}: current factory, source, bindings and semantic pin agree')


if __name__ == '__main__':
    main()
