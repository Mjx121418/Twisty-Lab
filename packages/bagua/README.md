# Experimental Bagua Cube

146 pieces, 198 local ports, six face axes and 45° stops. The production preset uses the domain-free relational C++ core, with polyhedral, spherical and port-diagram interpretations. It has not yet received independent whole-model human review.

See the [model specification](../../docs/bagua-model.md) for construction, notation, sources, checks and limits, and the [verification record](../../docs/bagua-verification.md) for published effects, the independent geometric audit and the remaining physical replay requirement. `review.json` pins the original semantics and independent solving-guide fixtures. The legacy definition remains an explicit comparison oracle and import/conversion source.

Load `relational-package.json` and the corresponding `relational-bagua-*.json` realization. Eight authoring domains flatten to 2,232 placements; occupancy, exclusions, and blocking use the same membership/count predicates as the other presets. The expanded AST is below 8 MiB, and the approximately 9.3 MiB package fits the separate 16 MiB execution-document limit. See [the migration record](../../docs/core-language.md#bagua-migration).

The [spherical construction](../../docs/spherical-bagua.md) uses six approximately 66.11° disks and twenty shared assets, retaining the existing pieces, ports, placement catalog and semantic digest. Bagua opens in **Cube + sphere** in the reference application.

Recompute with `python3 scripts/bagua_model.py`; use `--write` only after reviewing a model change. The generator uses bounded caches and a 384 MiB process ceiling. Run it separately from builds and browser tests.

Then reproduce relational sources and pins with `python3 scripts/bagua_packages.py`; `--write` runs the native parity gate against fresh temporary authoring data before updating files. Sessions use schema 2 and record completed movement tuples. Legacy schema-1 saves are unsupported by the current runtime.
