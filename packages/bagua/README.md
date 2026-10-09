# Experimental Bagua Cube

146 pieces, 198 local ports, six face axes and 45° stops. This finite model uses the existing C++ placement-relation core, with polyhedral, spherical and port-diagram interpretations. It has not yet received independent whole-model human review.

See the [model specification](../../docs/bagua-model.md) for construction, notation, sources, checks and limits, and the [verification record](../../docs/bagua-verification.md) for published effects, the independent geometric audit and the remaining physical replay requirement. `review.json` pins the current semantics and independent solving-guide fixtures. The compact generated documents stay below the kernel's 4 MiB input bound.

The [spherical construction](../../docs/spherical-bagua.md) uses six approximately 66.11° disks and twenty shared assets, retaining the existing pieces, ports, placement catalog and semantic digest. Bagua opens in **Cube + sphere** in the reference application.

Recompute with `python3 scripts/bagua_model.py`; use `--write` only after reviewing a model change. The generator uses bounded caches and a 384 MiB process ceiling. Run it separately from builds and browser tests.
