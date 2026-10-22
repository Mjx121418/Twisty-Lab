# Bagua Cube

146 pieces, 198 local ports, six face axes and 45° stops. The production preset uses the domain-free relational C++ core, with polyhedral, spherical and port-diagram interpretations. It has not yet received independent whole-model human review.

See the [model specification](../../docs/bagua-model.md) for construction, notation, sources, acceptance checks and the remaining physical replay requirement. Published solving-guide algorithms are retained in `tests/fixtures/bagua-algorithms.json`. The retired definition, duplicate realizations and historical review pin have been removed.

Load `package.json` and the corresponding `bagua-*.json` realization. Eight authoring domains flatten to 2,232 placements; occupancy, exclusions, and blocking use the same membership/count predicates as the other presets. The expanded AST is below 8 MiB, and the approximately 9.3 MiB package fits the separate 16 MiB execution-document limit. See [the migration record](../../docs/core-language.md#bagua-migration).

The [spherical construction](../../docs/spherical-bagua.md) uses six approximately 66.11° disks and twenty shared assets, retaining the existing pieces, ports, placement catalog and semantic digest. Bagua opens in **Cube + sphere** in the reference application.

Recompute with `python3 scripts/bagua_model.py`; use `--write` only after reviewing a model change. The generator uses bounded caches and a 384 MiB process ceiling. Run it separately from builds and browser tests.

`python3 scripts/bagua_packages.py` is the equivalent package entry point; `--write` validates fresh temporary authoring data with the current native compiler before updating files. Finish authoring changes with `npm run build:puzzles` to refresh the full inventory and manifest. Sessions use schema 2 and record completed movement tuples. Legacy schema-1 saves are unsupported by the current runtime.
