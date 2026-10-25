"""Check the immutable oracle captured with the pre-refactor 20fcdce executor.

The fixture includes old packages, fingerprints, effective local maps, and
normalized predicate/action signatures. Never regenerate it with new code.
"""
import gzip
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='twisty-reference-parity-') as temporary:
    path = Path(temporary) / 'oracle.json'
    with gzip.open(sys.argv[2], 'rt') as fixtures:
        for line in fixtures:
            path.write_text(line)
            result = subprocess.check_output([sys.argv[1], '--check', str(path)], cwd=root, text=True)
            print(result.strip(), flush=True)
