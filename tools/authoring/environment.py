"""Explicit host paths for model subprocesses; no build or kernel logic."""
import json
import os
from pathlib import Path
import shutil

PACKAGE = Path(__file__).resolve().parent
TOOL_NAMES = ('twisty', 'twisty_language', 'melty_solids', 'twisty_artifact_export', 'twisty_artifact_validate')


def workspace_root():
    return Path(os.environ.get('TWISTY_AUTHORING_WORKSPACE', os.getcwd())).resolve()


def native_tool(name):
    configured = json.loads(os.environ.get('TWISTY_AUTHORING_TOOLS', '{}')).get(name)
    found = configured or shutil.which(name)
    if not found:
        raise ValueError(f'Missing native tool {name}; provide an explicit tool path or tools directory.')
    return Path(found).resolve()


def explicit_tool(value, name):
    return Path(value).resolve() if value else native_tool(name)


def selected(name):
    selection = os.environ.get('TWISTY_AUTHORING_PROJECT')
    return selection is None or name in selection.split(',')


def data_file(name):
    if Path(name).name != name:
        raise ValueError('Expected a bundled authoring data filename.')
    return PACKAGE / 'data' / name
