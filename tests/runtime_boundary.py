"""Ensure distributed archives cannot reintroduce the retired evaluator."""
from pathlib import Path
import re
import subprocess
import sys

include_root = Path(__file__).resolve().parent.parent / 'include'
pending = [include_root / 'twisty/runtime.hpp', include_root / 'twisty/completion.hpp']
visited = set()
while pending:
    header = pending.pop()
    if header in visited:
        continue
    visited.add(header)
    for include in re.findall(r'^\s*#include\s*[<"]([^>"]+)', header.read_text(), re.M):
        assert 'authoring/' not in include and not include.startswith('nlohmann/'), header
        for candidate in [header.parent / include, include_root / include]:
            if candidate.is_file():
                pending.append(candidate.resolve())
                break

for archive in sys.argv[1:]:
    symbols = subprocess.check_output(['nm', '-C', '-g', archive], text=True)
    for retired in ['twisty::load_definition(', 'twisty::compile_source(',
                    'twisty::compile_gear_source(', 'twisty::validate_state(',
                    'twisty::legal_operations(', 'twisty::plan(',
                    'twisty::Execution::legacy(', 'twisty::Session::convert_legacy_json(']:
        assert retired not in symbols, f'{Path(archive).name} contains {retired}'
    if 'puzzle_runtime' in Path(archive).name:
        for dependency in ['twisty::language::', 'nlohmann::', 'twisty::Geometry', 'twisty::Execution']:
            assert dependency not in symbols, f'Runtime descriptors depend on {dependency}'
    if Path(archive).stem in ['libpuzzle_artifact', 'libpuzzle_execution_runtime',
                              'libpuzzle_session_runtime', 'libpuzzle_geometry_runtime', 'libpuzzle_controller']:
        for dependency in ['twisty::language::', 'twisty::Execution::from_source(',
                           'twisty::Session::Session(std::__cxx11::basic_string']:
            assert dependency not in symbols, f'Artifact runtime depends on {dependency}'
print('Production archives exclude retired backends; artifact runtime excludes source authoring.')
