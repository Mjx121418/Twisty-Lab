# Placement actions and compact twist families

Implement the approved two-extension plan with global placement IDs, unchanged
IN/RANGE permission semantics, and immutable compact transports. The anchor is
a coordinate convention, never a required legal intermediate stop.

- [x] Core finite groups/actions, placement collections, reference permutations,
  bindings, differences and finite unions; independent exact tests.
- [x] Version-2 JSON/compiler and native/WASM queries; version-1 pins preserved.
- [x] Shared interpreter transport views instead of dense request tables.
- [x] Migrate Cube/Bandage, Helicopter, Bagua, Gear and Melty A–D with full row parity.
- [x] Regenerate pins/manifests/fixtures, verify native/WASM, report storage and timing.

Migration may use extra charts or explicit maps for exactness. Report every
coverage fallback; never silently omit a participating row. Old definitions
remain readable; migrated definitions get new pins. No save conversion.

Heavy work is sequential, one build/browser worker, within 4 GiB. Dockerfile
and unrelated workspace edits must remain untouched. No push or deployment.

## Migration results

All eleven requested presets use version-2 sources/packages. The old source
format, definition fingerprint and session replay are covered by a frozen
version-1 fixture. Placement IDs, piece IDs, requests, choices, events and
participating self-loops match the pre-extension transport oracle.

Helicopter requires a second chart for each of its 36 domain/grip combinations.
Melty A–D construct 111 / 74 / 113 / 114 additional piece/grip charts, respectively.
Bagua needs no extra chart. No missing-coverage explicit fallback is needed.
The storage pass retains small transports as exact explicit rows when that
costs less than their exclusive union graph, counting shared templates and
reverse indices once. It selected 108 / 30 / 1,119 / 756 / 1,169 / 1,932 leaves
for Helicopter, Bagua, and Melty A–D. Unused descriptors are then removed.

## Storage and timing observations

These are single warm native Debug runs on the current container. Each sample
plans 100 forward moves and their 100 inverses, including state guards and the
package adapter. Load time measures abstract execution construction, excluding
geometric realization compilation. Both old and new packages use the current
runtime, so the timing comparison does not include the former dense adapter.
Small timing differences are not statistically established regressions or gains.

Core payload includes groups, collection/local lookup indices, bindings,
permutations, reverse indices and transport descriptors, normalized to 32-bit
words. It excludes JSON, predicate storage, allocator/container overhead and
interpreter view objects. The extra group capability adds fixed overhead on
small puzzles, so core payload alone can grow despite removing dense
interpreter tables.

| Preset | Package MB, before → after | Core payload KiB, before → after | Load ms, before → after | Plan µs, before → after |
| --- | ---: | ---: | ---: | ---: |
| cube3 | 0.13 → 0.13 | 1.9 → 4.5 | 24 → 23 | 163 → 270 |
| bandaged | 0.18 → 0.17 | 2.7 → 5.2 | 37 → 35 | 178 → 176 |
| helicopter | 2.15 → 1.89 | 29.5 → 37.7 | 747 → 796 | 3302 → 3215 |
| bagua | 9.70 → 8.70 | 159.4 → 147.5 | 3884 → 4150 | 29817 → 29197 |
| gear | 0.72 → 0.58 | 15.1 → 14.3 | 155 → 129 | 180 → 190 |
| gear-extreme | 0.57 → 0.42 | 15.1 → 14.3 | 127 → 105 | 178 → 185 |
| gear-plain | 0.26 → 0.12 | 15.1 → 14.3 | 75 → 53 | 166 → 165 |
| melty-a | 7.29 → 5.05 | 1087.2 → 512.0 | 4070 → 3289 | 5556 → 5582 |
| melty-b | 3.53 → 2.61 | 502.2 → 281.5 | 1875 → 1466 | 3478 → 3472 |
| melty-c | 7.66 → 5.27 | 1240.0 → 584.6 | 4529 → 3613 | 6529 → 5299 |
| melty-d | 14.40 → 9.31 | 2493.6 → 969.6 | 10000 → 7414 | 8195 → 7847 |

The removed dense endpoint/role payload alone occupied approximately 369 KiB
for Helicopter, 327 KiB for Bagua and 9.42 MiB for Melty D. Shared lookup views
and sparse unavailable-placement certificates replace those tables; they have
their own small per-view overhead and do not retain expanded derived maps.

The benchmark is reproducible with `build/native/transport_bench package.json`.
Transport parity uses `tests/fixtures/transport-baseline.json.gz`, an immutable
pre-extension oracle, through `compact_transport_parity`. Update that oracle
only for a deliberately reviewed change to puzzle semantics.

## Verification

- Native group/action tests cover noncommuting actions, marked/unmarked cube
  centers, partial domains, self-loops and malformed descriptors.
- Frozen transport parity: 3,765,986 exact source/target/participation checks
  across all eleven migrated presets, including version-1 fingerprint/replay.
- Native geometry, session, fixture and Melty witness suites passed.
- Full exporter reproducibility passed for every preset and realization.
- The WASM kernel accepts all 19 presets and 37 realizations; schema/pin checks pass.
- Independent installed native and copied WASM consumers agree, including the new
  action/reference example, all preset/view combinations and custom Gear masks.
- The independent language oracle agrees on 13,254 native/WASM queries.
- All 70 unit tests and 28 headless browser tests passed; production build passed.
- Realization data is unchanged apart from semantic pins; Dockerfile is unchanged.
- Heavy jobs ran sequentially with one build/browser worker. No cgroup OOM events.

Core implementation and verification are complete. No push or deployment was made.
