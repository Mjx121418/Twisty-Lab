# Experimental Helicopter Cube package

The production preset uses `package.json`: the abstract AST in `puzzle.pzl.json` plus separate `bindings.json` port/event metadata. Three authoring domains flatten to 420 placements and preserve all 44 identities. General predicates express jumble obstruction, occupancy, and each mechanism's own axis phases.

`helicopter-{euclidean,port-diagram,spherical}.json` contain the existing separate meshes, labeled ports, placement frames, and angular tracks, pinned to the compiled semantics. They are loaded by the C++ interpreter and drawn by renderer clients. The abstract package contains no geometry.

Retired definitions, duplicate realizations and the old shape-graph review pin have been removed. Historical verification findings remain in the [model specification](../../docs/helicopter-model.md) and Git history; legacy definitions and schema-1 saves are unsupported.

See the [language description](../../docs/core-language.md#helicopter-migration) and [model specification](../../docs/helicopter-model.md). Check reproducibility with `python3 scripts/helicopter_packages.py`; regenerate with `--write` after building `twisty`. The generator compiles the current package before updating realization pins. Finish authoring changes with `npm run build:puzzles` to refresh the full inventory and manifest.
