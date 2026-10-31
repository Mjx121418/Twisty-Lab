"""Check the immutable oracle captured with the pre-refactor 20fcdce executor.

The fixture includes old packages, fingerprints, effective local maps, and
normalized predicate/action signatures. Never regenerate it with new code.
Accepted later rule revisions have separate historical source and compiled pins;
they retain the original compiler and transport checks.
"""
import gzip
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
if len(sys.argv) not in (3, 4):
    raise SystemExit('Usage: reference_parity.py EXECUTABLE ORACLE_GZIP [RULE_REVISIONS]')
revisions_path = Path(sys.argv[3]) if len(sys.argv) == 4 else root/'tests/fixtures/reference-rule-revisions.json'
revisions = json.loads(revisions_path.read_text())
if revisions.get('formatVersion') != 1 or not isinstance(revisions.get('revisions'), dict):
    raise SystemExit('Unsupported rule revision references')
subprocess.run([sys.executable, '-B', str(root/'tests/reference_parity_policy.py'), sys.argv[1]], check=True)
seen = set()
with tempfile.TemporaryDirectory(prefix='twisty-reference-parity-') as temporary:
    path = Path(temporary) / 'oracle.json'
    with gzip.open(sys.argv[2], 'rt') as fixtures:
        for line in fixtures:
            name = json.loads(line)['name']
            if name in seen:
                raise SystemExit(f'Duplicate frozen reference {name}')
            seen.add(name)
            path.write_text(line)
            result = subprocess.check_output([sys.argv[1], '--check', str(path), str(revisions_path)], cwd=root, text=True)
            print(result.strip(), flush=True)
if set(revisions['revisions']) - seen:
    raise SystemExit('Rule revision references an unknown frozen puzzle')
