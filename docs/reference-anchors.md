# Correspondence anchors and placement references

The kernel uses two finite coordinate sets for a twist family:

- A contains correspondence anchors, one per consistent component of the
  position/placement correspondence graph.
- R contains placement references, with an injective binding B into the actual
  global placement catalog.
- Each position supplies an injective partial map P: A → R. Only its participating
  anchors need images, so |A| may exceed |R|.

An anchor identifies the same correspondence across positions. It is not a
piece, placement, angular position, or state variable. It needs no B entry. The
nominal anchor position need not be legal, an identity map, or defined on every
anchor. Correspondences can first appear at two nonzero positions.

The transport is unchanged: `B ∘ P_target ∘ inverse(P_source) ∘ inverse(B)`.
Inverse position lookup goes from R back to A. Undefined rows do not participate;
defined self-loops still produce their original events.

`PositionMap` stores source and target space IDs. A family declares the exact
anchor and reference space IDs shared by every position, and its binding uses R.
Derived maps remain `output ∘ base ∘ inverse(input)`: the base must match both
spaces, the input action acts on A, and the output action on R. These actions
may use different groups. Interning includes both space IDs. Construction checks
dimensions, partial injectivity, dependencies, group/action laws and resource
limits. Reverse indices are built only for explicit stored rows and bindings.
Derived maps and interpreter lookups remain compact.

## Compatibility and public interfaces

Kernel **0.7.0 / API 7**, language API **3**, source/package/definition/preview
**3**, and compact schema **2** introduce the rectangular carrier. JSON uses
`positionMaps[].sourceSpace/targetSpace` and family `anchorSpace/referenceSpace`.
The [language guide](core-language.md#compact-format-version-3) and
[API guide](kernel-api.md#api-7-correspondence-anchors-and-placement-references)
describe the native and WASM/SDK queries.

Source versions 1 and 2 remain readable with their original canonical shape
and fingerprints. Version-2 `permutations[].space` is the square special case
with equal source and target spaces. Cube/Bandage and Gear factories still emit
that supported format. Migrated Helicopter, Bagua and Melty packages receive new
pins; saves still require their matching definitions. No conversion is inferred.
Predicate version, state representation, session schema, controller/geometry
APIs and binary buffers are unchanged. No puzzle rules or motion paths changed.

## Helicopter factoring

An independent traversal of the frozen correspondence graph yields, for every
grip, before group reuse and storage selection:

| Category | Anchors | Bound references | Positions |
| --- | ---: | ---: | ---: |
| Center | 24 | 16 | 6 |
| Corner | 78 | 42 | 6 |
| Mechanism | 10 | 6 | 6 |

Each category fits one family per grip. The final package shares the center and
corner families across equivalent grips, retaining **two families and twelve
position maps** in total. The mechanism transports are cheaper as group/explicit
maps. No fake placements, extra anchor bindings, or size-driven charts are used.
The graph tests derive the counts from named requests and controller metadata;
the compressor does not hardcode these numbers or numeric catalog IDs.

## Storage choices and measurement

Removing chart splits changes template sharing. Selecting representations one
map at a time can retain an expensive family because no single map owns it.
The authoring pass also considers groups of maps sharing a family, then domains
and the full set. It replaces them with declared sparse maps only when their
combined retained payload is smaller. The estimate includes groups, actions,
both spaces, bindings, position maps, reverse indices and descriptors, counting
shared data once. Runtime never silently expands a derived transport.

Measurements compare against commit `20fcdce`, using the same Debug build and
container. Payload is retained 32-bit transport data, excluding JSON,
predicates, allocator/container overhead and interpreter assets. Lookup timings
use the median of three sweeps of 500,000 queries, including absent rows, over
the same sequence of named local moves and global placements. They are wall
times, so small differences on unchanged presets are noise rather than proof
of a speed change.

| Preset | Package bytes, before → after | Transport payload bytes, before → after | Lookup ns, before → after |
| --- | ---: | ---: | ---: |
| helicopter | 1,960,647 → 1,948,891 | 49,392 → 41,176 | 264.3 → 241.1 |
| bagua | 8,696,665 → 8,687,868 | 150,992 → 149,500 | 976.2 → 757.0 |
| melty-a | 3,241,842 → 3,178,003 | 361,456 → 227,224 | 115.5 → 72.7 |
| melty-b | 1,826,165 → 1,768,736 | 248,216 → 143,188 | 115.8 → 81.9 |
| melty-c | 4,696,489 → 4,599,523 | 480,212 → 280,684 | 207.2 → 84.0 |
| melty-d | 9,093,436 → 8,927,641 | 885,216 → 531,380 | 322.1 → 87.6 |

Cube/Bandage payload remains 4,624 / 5,300 bytes. Each Gear preset changes from
14,620 to 14,636 payload bytes because the new core metric counts the additional
space IDs in three position maps and one family; its version-2 JSON and
fingerprint stay unchanged. No material lookup regression was observed in the
six migrated presets. The small timing variation on unchanged presets is not
attributed to this refactor.

All Melty families are still constructed and checked from correspondence
components. After combined storage selection, their single-cap transports are
cheaper as explicit sparse maps, so no family templates remain in those runtime
packages. This is an authoring storage choice, not runtime expansion of a hidden
derived table. Helicopter retains two families; Bagua retains ten. Unions remain
where distinct phase cases need them. No missing-coverage explicit fallback was
required; manifests distinguish storage choices from coverage failures.

## Frozen semantics and validation

`tests/fixtures/reference-baseline.jsonl.gz` was captured with the **pre-refactor
20fcdce executor**, before editing its representation. It contains all eleven
compact packages and fingerprints, effective maps, and signatures of complete
IN/RANGE predicates, requests, profiles, selected action records and events.
The signatures replace numeric map/predicate IDs with their semantic content.
This oracle must not be regenerated from the new implementation.

`tests/reference_parity.py` checks every named map at every global placement,
including absent rows and self-loops. Numeric descriptor IDs may change during
factoring, so local move bindings identify the effective maps. It also checks
every permission/action signature and reloads old packages and canonical
definitions against their frozen fingerprints. The older pre-extension
transport oracle remains an additional compatibility check.

Focused examples cover A larger and smaller than R, partial domains, inverse
queries, independent C5/S3 input/output actions, correspondences missing at
position zero, malformed dimensions, non-injectivity, incompatible spaces,
cyclic/deep dependencies and participating self-loop events. Native/WASM and
installed-consumer checks use the same public queries. Existing permission,
replay, Melty witness, controller and geometric endpoint/path checks exercise
the migrated presets. Binding catalogs and all twenty affected realization
documents are checked unchanged except their semantic pins.

Heavy checks run sequentially with one build job and one headless browser
worker within the 4 GiB container limit. Dockerfile is untouched.

Verified results:

- Fifteen targeted native suites pass, including both frozen transport oracles,
  finite actions, authoring graphs, permissions, events, all Melty witness graphs,
  replay, controllers, runtime fixtures and interpreter certification.
- All 73 unit tests pass. The independent language oracle agrees on 13,254
  native/WASM state, choice and witness results across 34 models.
- Installed native and copied WASM consumers agree on 34 preset/view pairs,
  including rectangular forward/inverse queries and self-loop events.
- All eleven frozen format-2 fingerprints match both native and WASM loaders;
  native canonical definitions also round trip unchanged. The existing frozen
  format-1 fingerprint/replay check passes.
- The WASM kernel accepts all 19 generated packages and 37 realizations. Full
  package generation, schemas, pins, manifest freshness and production build pass.
- Eight headless browser regressions pass, covering the independent Canvas SDK,
  Helicopter and Melty views, controllers, history and saved sessions.
- A million-anchor sparse example retains the same transport payload as the
  five-anchor example. No container OOM events were recorded, and memory or
  compiler limits were not increased.

The frozen WASM check is included in `npm run check:kernel`; it can also run
alone with `node tests/reference_wasm.mjs build/kernel/generated/twisty.mjs`.
