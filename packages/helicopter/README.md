# Experimental Helicopter Cube package

The production preset uses `relational-package.json`: the abstract AST in `relational.pzl.json` plus separate `relational-bindings.json` port/event metadata. Three authoring domains flatten to 420 placements and preserve all 44 identities. General predicates express jumble obstruction, occupancy, and each mechanism's own axis phases.

`relational-helicopter-{euclidean,port-diagram,spherical}.json` contain the existing separate meshes, labeled ports, placement frames, and angular tracks, pinned to the new semantics. They are loaded by the C++ interpreter and drawn by renderer clients. The abstract package contains no geometry.

`definition.json` and the original realizations remain explicit legacy imports and comparison oracles. `review.json` pins that model for its independent shape-graph verifier. Legacy saves require the kernel's explicit checked conversion API before loading into the new preset.

See the [relational migration](../../docs/core-language.md#helicopter-migration) and [original model specification](../../docs/helicopter-model.md). Check reproducibility with `python3 scripts/helicopter_packages.py`; regenerate deliberately with `--write` after building `twisty` and `helicopter_language_tests`. Regeneration runs the parity gate before repinning. The original exact authoring outputs remain checked by `scripts/helicopter_model.py`.
