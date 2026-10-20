#!/usr/bin/env python3
"""Generate portable Gear language modules and rebind unchanged realizations."""
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
        wrapper = json.loads((directory / 'relational-source.json').read_text())
        with tempfile.TemporaryDirectory(prefix='twisty-gear-') as temporary:
            path = Path(temporary) / 'source.json'
            path.write_text(json.dumps(wrapper))
            module = json.loads(subprocess.check_output([str(native / 'twisty_language'), str(path), '--gear-module'], text=True))
            compiled = json.loads(subprocess.check_output([str(native / 'twisty'), 'compile', '--definition', str(path), '--json'], text=True))
            if compiled['status'] != 'Compiled':
                raise SystemExit(compiled)
        digest = compiled['definition']['definitionDigest']
        documents = {'relational-source.json': wrapper, 'relational.pzl.json': module['source'],
                     'relational-bindings.json': module['catalog'],
                     'relational-package.json': dict(kind='relational-package', formatVersion=1, **module),
                     'relational-manifest.json': dict(puzzleId=wrapper['puzzleId'], definitionDigest=digest)}
        for path in sorted(directory.glob(name + '-*.json')):
            model = json.loads(path.read_text())
            if not model.get('kind', '').startswith('articulated-'):
                continue
            model['compatibleDefinitionDigest'] = digest
            documents['relational-' + path.name] = model
        for filename, content in documents.items():
            path = directory / filename
            if args.write:
                path.write_text(json.dumps(content, separators=(',', ':'), sort_keys=True) + '\n')
            elif not path.exists() or json.loads(path.read_text()) != content:
                raise SystemExit(f'{path.relative_to(ROOT)} is stale; review before regenerating.')
        print(f'{name}: relational factory, source, bindings and unchanged realization tracks agree')


if __name__ == '__main__':
    main()
