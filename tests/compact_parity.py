"""Unpack the pre-extension transport oracle outside the repository tree."""
import gzip
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='twisty-transport-parity-') as directory:
    path=Path(directory)/'baseline.json'
    path.write_bytes(gzip.decompress(Path(sys.argv[2]).read_bytes()))
    subprocess.run([sys.argv[1],str(path)],check=True)
