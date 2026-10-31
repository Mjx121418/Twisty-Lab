#!/usr/bin/env python3
"""Compatibility command for staged reference builds and read-only validation."""
import argparse
from pathlib import Path
import subprocess
import sys

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--write',action='store_true')
parser.add_argument('--update-fixtures',action='store_true')
args=parser.parse_args()
if args.update_fixtures and not args.write: parser.error('--update-fixtures requires --write')
root=Path(__file__).resolve().parents[1]
if args.write:
    command=['node','scripts/build-puzzles.mjs']+(['--update-fixtures'] if args.update_fixtures else [])
else:
    subprocess.run(['node','scripts/check-puzzles.mjs','--files-only'],cwd=root,check=True)
    command=[sys.executable,'-B','-m','tools.authoring.legacy','validate','--workspace',str(root),'--tools-dir',str(root/'build/native')]
raise SystemExit(subprocess.call(command,cwd=root))
