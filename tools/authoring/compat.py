"""Compatibility bootstrap for the former scripts/ entry points."""
import importlib
import json
import os
from pathlib import Path
import runpy
import sys
from .environment import TOOL_NAMES


def forward(module, alias):
    root = Path(__file__).resolve().parents[2]
    os.environ.setdefault('TWISTY_AUTHORING_WORKSPACE', str(root))
    os.environ.setdefault('TWISTY_AUTHORING_TOOLS', json.dumps({name: str(root/'build'/'native'/name) for name in TOOL_NAMES}))
    name = 'tools.authoring.models.' + module
    if alias == '__main__':
        # Spawned workers need the real module's __spec__ and globals in
        # sys.modules['__main__'], just as with python -m.
        runpy.run_module(name, run_name='__main__', alter_sys=True)
    else:
        sys.modules[alias] = importlib.import_module(name)
