# Experimental Helicopter Cube package

`definition.json` contains only abstract placements, resources, ports, guards, and transports. `review.json` pins its semantic digest and supplies shape symmetries and published acceptance counts for the headless verifier.

`helicopter-euclidean.json` and `helicopter-port-diagram.json` contain the separate polyhedral models, labeled port faces, placement frames, and angular tracks used by the C++ interpreter. The Three.js browser preset displays both realizations.

See [the model specification](../../docs/helicopter-model.md) for scope, exact authoring, stop notation, geometric interpretation, verification, and remaining independent review. Regenerate deliberately with `python3 scripts/helicopter_model.py --write`; the default command checks all four files without changing them.
