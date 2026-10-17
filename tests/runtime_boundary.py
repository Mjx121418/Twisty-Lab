"""Ensure distributed archives cannot reintroduce the retired evaluator."""
from pathlib import Path
import subprocess
import sys

for archive in sys.argv[1:]:
    symbols = subprocess.check_output(['nm', '-C', '-g', archive], text=True)
    for retired in ['twisty::load_definition(', 'twisty::compile_source(',
                    'twisty::compile_gear_source(', 'twisty::validate_state(',
                    'twisty::legal_operations(', 'twisty::plan(',
                    'twisty::Execution::legacy(', 'twisty::Session::convert_legacy_json(']:
        assert retired not in symbols, f'{Path(archive).name} contains {retired}'
print('Production archives contain no legacy compiler, evaluator, or conversion symbols.')
