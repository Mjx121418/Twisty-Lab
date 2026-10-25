# Puzzle language and core executor

The language provides an expanded JSON AST, a portable C++ compiler, a domain-free executor, and separate completion helpers. All production presets use relational sessions and interpreters in the reference application and independent clients. The legacy evaluator and migration comparison tests have been removed. The [language roadmap](abstract-language-core-plan.md) lists remaining authoring and tooling work.

The implementation is available through `Twisty::core` (standard C++ library only), `Twisty::language` (compiler, completion helpers, and JSON facade), and the WASM/TypeScript `Puzzle` class. It is advertised as `languageAPIVersion = 2`. Public API 5 adds compact transports; API 4 established the current core/language names; see [the naming migration](kernel-api.md#api-4-naming). Format-1 definitions remain readable with their original pins. Migrated format-2 packages have new pins; complete-tuple session schema 2 and frame-buffer version 1 are unchanged. The program preview remains a separate inspection format without its own semantic digest or replay codec.

## Execution and completion

The executor takes a valid state, a request, and a **complete movement tuple**. It checks `R(s,u,a1,...,an)` and returns either a permitted atomic transition or rejection of that tuple. Checking involves no search or automatic rule propagation.

The compiler flattens the disjoint union of local catalogs. The execution data contains no domain names, piece types, or geometry. A state is one vector of global placement IDs, with per-piece placement compatibility compiled into the common invariant. Occupancy and other exclusions are authored invariants; they are not implicit runtime rules.

All predicates lower to `IN` and `RANGE` instructions. Constants are references 0 and 1; other references index `predicateNodes[ref - 2]`. `IN` reads a state or choice input and tests an interned finite set. `RANGE` counts true children through a packed slice of `childReferences`. Repeated children remain repeated counted terms. Nodes are shared and topologically ordered; evaluation uses an explicit stack rather than C++ recursion.

Selections decode the supplied choices. Local requirements and interaction clauses are tested on the original source, transports run simultaneously, and the same invariant is evaluated afresh on the tentative target. Missing transport rows mean no participation. Explicit self-loops retain participation and their opaque events. Multiple writers of a piece reject the tuple, even when their endpoints match. This AST uses one choice per domain and disjoint piece families, so its compiler structurally excludes conflicting local writers; the low-level executor also checks them defensively.

`bind_profile()` and `discover()` are outside the executor:

- Binding selects an explicitly named profile or an author-declared default whose state-only `when` predicate holds. Without either, it requests a choice. It never picks the first profile or claims a profile exhausts the relation. A selected tuple still needs execution permission.
- Discovery streams candidate tuples under separate candidate and result limits. `complete: false` means the search stopped early. An empty incomplete result proves nothing about the existence of a permitted tuple. `complete: true` means every tuple was checked. This is a small bounded exploration helper, not a general-purpose solver.

Distinct choices remain distinct even when their targets coincide. Every permitted transition includes its request, concrete choices, original and target states, and `{piece, from, to, event}` records. An interpreter binds those event IDs to motion tracks; endpoints alone cannot determine winding. Uniform catalogs bind face/stop tracks; articulated catalogs bind per-domain events independently.

## JSON AST version 1

The [complete example](../examples/language/coupled.pzl.json) covers synchronization, cyclic implications, alternatives, obstruction, fixed profiles, and different winding events. The [JSON schema](../schemas/puzzle.schema.json) describes the structural contract; the C++ compiler additionally checks names, types, supports, recursion, and bounds.

```json
{
  "kind": "relational-puzzle",
  "languageVersion": 1,
  "id": "two-components",
  "domains": [
    {
      "id": "corners",
      "placements": ["a", "b"],
      "pieces": [{"id": "corner-1", "initial": "a"}],
      "moves": [{"id": "swap", "support": ["a", "b"],
                 "transport": {"a": "b", "b": "a"}}]
    },
    {
      "id": "edges",
      "placements": ["a", "b"],
      "pieces": [{"id": "edge-1", "initial": "a"}],
      "moves": [{"id": "swap", "support": ["a", "b"],
                 "transport": {"a": "b", "b": "a"}}]
    }
  ],
  "requests": [{
    "id": "turn",
    "relation": {"all": [
      {"chosen": {"domain": "corners", "moves": ["swap"]}},
      {"chosen": {"domain": "edges", "moves": ["swap"]}}
    ]},
    "profiles": [{"id": "both", "choices": {"corners": "swap", "edges": "swap"}}],
    "defaultProfile": "both"
  }]
}
```

Domain order determines the companion choice-slot order. Each slot's movement values start with implicit `idle`, followed by its declared moves. `idle` cannot be redefined. Piece names are globally unique. Placement references in expressions use `domain/localName`; initial placements, supports, and transport targets use local names. Catalogs remain separate even when local names are identical.

A move applies its transport to all pieces in its domain, or to the optional explicit `pieces` subset. Its table must have exactly the declared source support, known targets, and unique targets. `requires` defaults to `true` and must depend only on state. `event` defaults to `domain/move`; it is an opaque name, not an angle or direction. A simultaneous composition within a domain can already be supplied as one explicit exact transport and event; automatic composition syntax is deferred.

Supported expressions are:

| Expression | Meaning |
| --- | --- |
| `true`, `false` | Constants |
| `{"at":{"piece":"p","placements":["d/q"]}}` | Placement membership |
| `{"occupiedBy":{"placement":"d/q","pieces":["p","r"]}}` | At least one named piece occupies that placement |
| `{"chosen":{"domain":"d","moves":["idle","m"]}}` | Supplied local choice membership |
| `{"request":["u","v"]}` | Request membership, specialized by compilation |
| `{"all":[...]}`, `{"any":[...]}`, `{"not":...}` | Boolean conjunction, disjunction, negation |
| `{"implies":[a,b]}`, `{"iff":[a,b]}` | Implication and equivalence |
| `{"count":{"min":k,"max":l,"of":[...]}}` | Inclusive true-term count, preserving multiplicity |
| `{"ref":"name"}` | A definition from top-level `predicates` |

Empty `all` is true; empty `any` is false; impossible count intervals are false. Named predicates must be nonrecursive. Cyclic *constraints*, such as `(a implies b) and (b implies a)`, are ordinary Boolean expressions and allowed. All names and operand types are checked even in branches that fold to a constant.

Top-level `relations: [{id, predicate}]` are conjoined with each request's `relation` and selected local requirements. The request leaf lets a relation refer to `u`. Invariant, goal, local requirements, and profile conditions are state-only; they cannot read a choice or request. The invariant defaults to placement compatibility alone. The goal defaults to the initial assignment and never restricts permission. Profile declarations select candidates and do not add clauses to the relation.

The AST is the initial expanded form. The cube/bandage authoring library now supplies finite symmetry expansion and exact profiles. Textual `.pzl` syntax, imports, general reusable domain modules/templates, automatic profile derivation, source-level conflict compositions, remain subsequent work. No runtime callbacks or puzzle-specific predicate handlers are used here.

## Cube and bandage migration

The portable authoring library generates ordinary version-1 ASTs directly from the 24-element permutation action on six abstract face labels. It does not load a legacy definition or call the legacy planner. The authoring compiler uses the standalone `Twisty::symmetry` library for finite permutation/group/coset operations; that library depends only on standard C++.

| Model | Persistent pieces | Flat placements | Directed requests |
| --- | --- | --- | --- |
| 3×3 cube | 26 | 54 | 12 |
| UF–UFR bandaged cube | 25 | 78 | 12 |

Corners, edges, and centers each have local transports. Every request permits exactly the tuple selecting its face turn in each domain, with an explicit default profile supplying that tuple. Center self-loops retain the quarter-turn event. The compound module adds its 24 rigid placements and replaces the two fused identities with one `bandage/UF-UFR` identity.

Bandage blocking is an interaction clause: selecting a local face movement implies the compound is outside the placements cut by that movement. The compound transport table contains only fully participating placements. Occupancy is a conjunction of ordinary exact-one cardinality formulas, one per abstract slot; the compound contributes to both slots it occupies. There is no footprint handler in the executor.

The generated artifacts are:

- [Cube AST](../packages/cube3/puzzle.pzl.json) and [catalog bindings](../packages/cube3/bindings.json).
- [Bandage AST](../packages/bandaged/puzzle.pzl.json) and [catalog bindings](../packages/bandaged/bindings.json).

The separate catalogs preserve piece names, local ports, port labels, abstract resource incidence, qualified placement names, inverse requests, and event names. They contain no coordinates, meshes, or rotation angles. These are the bindings needed for the later interpreter migration, rather than extra fields in the executing core.

Native clients can call `cube_module(false)` or `cube_module(true)` from `<twisty/authoring/cube.hpp>` and pass its `source` to `compile()`. WASM exposes `cubeModuleJSON(bandaged)`; TypeScript clients can use:

```js
const { source, catalog } = createCubeSource(kernel, true);
const puzzle = new Puzzle(kernel, JSON.stringify(source));
// catalog carries names/ports/events; puzzle carries the executable program.
```

Import `createCubeSource` from the SDK entry point. Libraries are advertised in `kernelInfoJSON().authoringLibraries`. Checked-in ASTs can also be loaded directly without invoking the authoring library.

Regenerate or check the artifacts after building the native example:

```sh
python3 scripts/cube-packages.py --write
python3 scripts/cube-packages.py
```

`twisty_language --cube-module` and `--bandaged-module` print the source/catalog pair. The package checker includes their reproducibility check. Existing schema-1 definitions and their pins remain static geometry catalogs and archived review data only. Production cube/bandage sessions use relational packages and schema-2 histories; legacy imports and conversion methods have been retired in API 3.

## Native use

```cpp
#include <twisty/completion.hpp>

auto puzzle = twisty::language::compile_text(source_text);
auto state = puzzle.program.data().initial;
auto choices = twisty::language::bind_profile(puzzle, 0, state);
if (choices) {
  auto plan = puzzle.program.plan(0, state, *choices);
  if (plan.transition) state = plan.transition->after;
}
```

`Program` validates its data at construction and exposes only const execution data. Each query owns its scratch; a const program can be shared across threads. The compiler package and its metadata should be treated as immutable. Native input/program errors throw `core::Error` with a `code`. A well-formed but forbidden tuple returns a `Plan` rejection. No session state is mutated by planning, binding, discovery, or goal tests.

For optional native diagnostics, `Program::predicate_trace(root, state, choices)` returns an owned vector indexed by predicate reference: evaluated values are 0 or 1, with −1 for unvisited nodes. It checks input shape, not invariants or request permission. The separate `language::rejection_inputs()` helper follows a sufficient Boolean proof for a rejected relation and returns owned state-input indices; it adds no predicate kinds or completion policy to the core.

The independently linkable example accepts a source path and one JSON query per stdin line, or `--inspect` for the compiler preview:

```sh
cmake --preset native
cmake --build --preset native --target twisty_language core_tests
build/native/twisty_language examples/language/coupled.pzl.json --inspect
ctest --preset native -R 'core_relations|language_source_oracle|cube_packages'
```

The example's separate CMake project can also consume an installed `Twisty::language` package. Linking only `Twisty::core` adds neither the JSON compiler nor the existing executor/geometry.

## WASM and TypeScript use

```js
import { createKernel, Puzzle } from '@twisty/kernel';
const kernel = await createKernel();
const puzzle = new Puzzle(kernel, sourceText);
try {
  const state = puzzle.preview.program.initialPlacements;
  const candidate = puzzle.query({ op: 'bind', state, request: 'fixed' });
  if (candidate.status === 'Selected') {
    const result = puzzle.query({ op: 'plan', state, request: 'fixed', choices: candidate.choices });
    // Only result.status === 'Permitted' provides a transition.
  }
} finally { puzzle.dispose(); }
```

The low-level equivalents are `new kernel.Puzzle(sourceText)`, `inspectJSON()`, and `queryJSON(queryText)`. `compilePuzzleJSON(sourceText)` returns `Compiled` with a preview, or `Invalid` with diagnostics, without retaining a handle. Constructors throw on invalid source. Query JSON errors return `Invalid`; an invariant-invalid source differs from a valid source with a rejected tuple.

Every query supplies `state: number[]`. The supported operations are:

| `op` | Additional fields | Result |
| --- | --- | --- |
| `validate` | None | `Valid` or `Invalid` |
| `goal` | None | `Evaluated`, `solved` |
| `plan` | `request`, complete `choices: string[]` | `Permitted` with transition, or `Rejected` with reason |
| `bind` | `request`, optional `profile` | `Selected` with candidate choices, or `ChoiceRequired` |
| `discover` | `request`, `candidateLimit`, `resultLimit` | `Discovered` with choices, `examined`, and `complete` |

Placement, piece, and event IDs in results index the companion bindings arrays. Choice names follow `bindings.choiceSlots`. Supplied choices must include every idle slot; the core does not infer omitted values. All returned JSON and JS arrays are owned copies. Call `delete()` on the low-level handle or `dispose()` on the adapter. There are no borrowed memory views or implicit animation clocks in this API.

Source text is limited to 8 MiB and nesting 256; expressions expand to at most 100,000 shared nodes, 1,000,000 child references, and 2,000,000 finite-table entries. Limits reject oversized expansion rather than enumerating full puzzle states. Discovery additionally limits candidates to 1,000,000 and returned tuples to 10,000. Production catalogs may require measured adjustments; these limits are not a guarantee that every finite relation has a small representation.

## Verification

The native C++ tests exercise atomic execution, input/program validation, conflicting writers, source/target predicate rebinding, self-loop participation, deep graphs, candidate binding, and truncated/exhaustive discovery. The independent Python evaluator reads the source AST directly and exhausts tiny global assignments and local tuples, including invalid cross-catalog imports. Its expected states and witnesses never use compiled predicates or maps. Seeded Boolean formulas and a separate occupancy model broaden the comparison.

After building native, WASM, and the packaged SDK, run `npm run check:language`. Optional positional arguments select the native example executable and SDK entry point when using separate build directories. That check compares both the native consumer and WASM API with the source oracle, including complete transition witnesses and compiler previews.

The completed cube/bandage migration compared every local transport entry, all 24 whole-cube orientations, deterministic sampled walks, labels, ports, and complete event witnesses against the previous executor. Those comparison executables and their exported WASM cases have been retired. Current compiler/core tests, package reproducibility, native/WASM unit checks, and installed/copied SDK consumers exercise the current authoring library and programs directly.


## Session and interpreter migration

Current package checks reproduce cube/bandage `relational-package` documents and all three realization pins. The reference app, default headless CLI, and independent Canvas example use those packages. `Twisty::execution` is a transitional wire/frame adapter: it maps the existing symbolic inspector state and grouped port frames to the domain-free executor. All new permission, validity, and goal checks use `core::Program`; the shipped runtime has no alternative execution backend.

Public API 4 retains persisted definitions and session schema 2 with complete choices, with the legacy runtime APIs removed. Planning, interactive/transactional algorithms, revision checks, seeded scrambles, undo/redo, and history loading share this execution context. The interpreter also shares it and validates the concrete tuple and event witness. The existing standalone preview format and its version remain experimental and unchanged. See [the session contracts](kernel-api.md#sessions-and-checked-replay).

The adapter accepts the parity-gated cube catalog and the uniform port catalog used by Helicopter. It requires reciprocal named moves and one uniform event per request. It is not a general realization compiler for every raw AST. Richer source diagnostics and textual/module syntax remain ahead. Articulated catalogs now bind local events to certified request tracks. The new core's two predicate opcodes and disjoint placement layout are unchanged.


## Helicopter migration

[The Helicopter AST](../packages/helicopter/puzzle.pzl.json) is authored offline by `scripts/helicopter_packages.py` from the same exact rational catalog as the original model. No legacy compiler or planner is called to generate its movement tables. The separate [port catalog](../packages/helicopter/bindings.json) and compact `package.json` load through the portable API without Python.

Its three authoring domains have 144 center, 240 corner, and 36 mechanism placements. All 44 identities survive, including twelve distinct mechanism pieces. Ordinary membership invariants restrict each mechanism to its own three axis phases. The compiler flattens the disjoint catalogs to 420 placements; domain names never enter the core. Predicate lowering produces 8,847 predicate nodes, 53,061 child references, and 7,920 action records. The earlier explicit representation used 276 sparse maps; format 2 shares group actions and reference families instead. Occupancy and exclusion use shared location predicates and capacity counts. Each of 180 directed stop requests relates its three local choices and prohibits occupied unavailable locations. A mechanism move's source-phase requirement also applies to explicitly supplied tuples.

The declared default profile is available only in its source phase. `snapshot.candidateRequests` lists the 60 bound defaults before checking permission; `legalRequests` keeps only permitted candidates. This lets controls show five destinations per current grip while retaining blocked requests for explanations. Neither list is exhaustive discovery of arbitrary completions. Request selection remains outside the core.

`relational-port-bindings` version 1 preserves ports, frames, inverse names, and uniform stop events. `unavailablePlacements` certifies the interpreter's cut sections; it is frame metadata, never a runtime guard. Existing polyhedral, spherical, and port-diagram meshes and sampled tracks are unchanged. New `helicopter-*.json` files carry the new semantic pin and the merged mechanism authoring-domain binding. Saves use schema 2; old saves require conversion with a previous release.

The completed migration was checked against the previous executor before that evaluator was retired. Current checks exercise the production kernel directly: package reproducibility, mechanism-axis restrictions, blocking, inverse/replay behavior, full native/WASM witnesses, and all three interpreters. The historical migration comparisons and exhaustive legacy shape-graph verifier are no longer built.

Build `twisty_language` and `twisty`, then regenerate with `python3 scripts/helicopter_packages.py --write`. Generation compiles the fresh candidate before changing source files or realization pins. Omit `--write` for the reproducibility check.


## Gear family migration

The portable `gear_module(gearedEdges, puzzleId)` authoring library in `<twisty/authoring/gear.hpp>` derives ordinary ASTs from the new cube library and exact finite face-label permutations. It never calls the old compiler or executor. Normal, Extreme, all-plain, and custom masks retain 26 identities and 198 flat placements: six center, 24 corner, 24 plain-edge, and 144 geared-edge placements. An empty plain/geared authoring domain is allowed. Gear identity is persistent; phase has six exact values.

Each domain supplies ordinary and coupled local movements. For each requested face and inverse, `R` selects the authored tuple and counts geared identities occupying the middle slice: ordinary requests require zero; coupled requests require one through four. Coupled tables turn both opposite faces, leave middle carriers fixed, and advance each driven cog phase with its intrinsic handedness. Occupancy is an ordinary count invariant shared across plain and geared edge domains. No new predicate opcode, guard class, core domain field, or automatic propagation rule is needed.

Runtime authoring uses `{kind: "relational-gear-prototypes", formatVersion: 1, puzzleId, gearedEdges}`. This is a new explicit source kind; `gear-cube-prototypes` is rejected by the runtime; historical source kinds are unsupported. C++ `gear_module_source()`, WASM `gearModuleJSON(sourceText)`, and TypeScript `createGearSource(kernel, source)` expose the library. The standalone native consumer accepts `source.json --gear-module`. `kernelInfoJSON().authoringLibraries` includes `gear-family`. Static ASTs and complete packages remain loadable without using the factory.

`relational-articulated-bindings` version 1 adds `moveInverses[domain][localMove]` and `events[event] = {domain, request}`. Inverse names must be reciprocal. An event certifies exactly the local movement selected by that request's declared default profile in the named domain. All local moves need such a certificate; these profiles select concrete moves in each slot. Event names are separate from legacy transport labels and contain no geometry. The kernel checks bindings when loading, rather than deriving a rotation from endpoints.

The interpreter verifies each certificate against the existing per-domain/per-placement tracks, then selects a track separately for every participant's recorded event. Different domains can execute different local movements in one permitted tuple; explicit idle choices leave their pieces stationary. Inverses and replay use recorded local choices. A separate synthetic gate exercises an alternative tuple whose corners follow R while other domains follow F, including animation, inverse restoration, and saved replay. This prevents the adapter from silently using one global request track for every piece.

The current supplied cubic and spherical meshes and their six-phase diagrams use full 300° cog tracks. Events and exact source placements choose the whole authored path, including self-loops. Current saved histories preserve the completed choices and full event witnesses; legacy save conversion has been retired. The earlier procedural models and their frontend selector have been removed.

Regenerate the Gear sources, catalogs and manifests with `python3 scripts/gear_packages.py --write`; the geometry generators derive their pins from the same current authoring factory. Finish changes with `npm run build:puzzles` to refresh the inventory and manifest. Historical migration comparisons have been retired; current unit and SDK checks cover custom masks, mixed coupling, phase/event witnesses, geometric motion, and native/WASM parity.


## Bagua migration

Bagua uses eight authoring domains, 146 identities, 2,232 disjoint placements, and 30 complete requests. Exact offline geometry authoring still constructs the finite catalog and exclusion resources. `scripts/bagua_packages.py` lowers those data to ordinary placement memberships, shared occupancy predicates, capacity counts, and cross-domain obstruction relations. Runtime permission, validity, and goals use the same core executor as every other preset. The port catalog retains unavailable-placement certificates for the interpreter, not executable guards.

The module compiles to 54,618 predicate nodes, 129,997 child references, and 4,380 action records. The earlier explicit representation used 240 sparse maps; format 2 shares group actions and reference families instead. Named source predicates share lowering across state and relation contexts, with request-dependent aliases specialized as needed; sharing does not weaken cycle, depth, or state-only checks. Boolean evaluation stops when a count bound is decided. Diagnostic traces remain exhaustive over reachable nodes so rejection proofs retain all relevant inputs. No predicate opcodes or puzzle-specific executor rules were added.

Generation compiles the fresh Bagua candidate through the current kernel before updating sources and realization pins. Build `twisty`, then use `python3 scripts/bagua_packages.py --write`; omit the flag for reproducibility checking. Current runtime, fixture, and native/WASM checks exercise blocking, published algorithms, witnesses, inverses, replay, and all three views. The historical migration comparison and legacy geometric-audit executables have been retired.

Imports use a string-aware linear depth scan followed by the normal JSON parser. This avoids quadratic discard scans in the callback parser while preserving its nesting boundary, quoted escapes, and syntax validation. In this container, Bagua first-session construction fell from roughly 17 seconds to 1.4 seconds in WASM; a 30-turn published sequence takes about 0.3 seconds.

The compact AST stays within the existing 8 MiB language limit; the complete package is approximately 9.3 MiB. Session/geometry handles share immutable compiled definitions. Creating a second session from an existing one starts with fresh state/history and avoids recompilation. Execution documents use a separate 16 MiB limit; the WASM heap ceiling remains 256 MiB.

All production presets and the independent Canvas client load relational packages. API 3 removed compatibility imports, schema-1 replay, and save conversion from the runtime. The old evaluator and native migration/model-audit executables have now been deleted. Production archives remain checked for retired symbols, and runtime integration tests and independent native/WASM consumers cover the current kernel. Textual syntax and general authoring modules/templates remain follow-ups.

## Prebuilt package workflow

Normal app builds consume committed puzzle packages. After using an individual exporter or changing its authoring inputs, finish with `npm run build:puzzles` to regenerate and validate all packages and update their deterministic input/artifact manifest. Fixture semantic identities change only with the explicit `-- --update-fixtures` option. `npm run check:puzzles -- --files-only` checks freshness without generators; `npm run check:puzzles` additionally validates schemas and compatibility with the built WASM kernel. The full regeneration-based audit remains `npm run check:packages`. These build commands do not change the language, runtime formats, or on-load compiler behavior.

## Compact format version 2

Version 1 remains supported with identical fingerprints. Version 2 keeps the
existing domains, predicates, local choices, events and requests, and adds a
`compact` object. A movement selects `map: numericId` instead of storing
`support` and `transport`; these two forms are mutually exclusive. Explicit
version-1 movement syntax is also accepted in a version-2 document. The source
and its package/definition must have matching versions.

The portable data declarations are:

| Field | Meaning |
| --- | --- |
| `groups` | Group order, row-major products, inverse elements; identity is 0 |
| `actions` | Group ID, set size, regular/tabulated/trivial action |
| `collections` | Action ID and local-to-global placement IDs; optional compiler name `id` |
| `spaces` | Standalone finite reference-set sizes |
| `permutations` | Injective sorted reference rows, or an earlier base with input/output actions |
| `bindings` | Injective reference-to-placement array, or an earlier binding transformed by a placement group element |
| `families` | Reference space and position-permutation IDs |
| `maps` | Explicit rows, restricted group action, bound difference, or finite union |

Collections partition the actual placements. Regular actions use group
multiplication directly. Tabulated actions use `table[g * size + q]`; trivial
actions require no table. A group action is structural information, not proof
that the final puzzle's guards or configured pieces preserve that symmetry.

For a family, each position supplies an injective partial map `P_theta`. The
transport is `B P_target inverse(P_source) inverse(B)`. The anchor is a
coordinate convention, never a required legal intermediate state. References
identify potential placements, not persistent pieces. A missing image is absent
from the participating support; a self-image remains a participating row.
Migration verifies the full support as well as every endpoint.

A derived position map is `out * base * inverse(in)`. Both actions must act on
the reference space; either may be omitted. A derived binding transforms all
base placements by the specified group, which must agree with each affected
placement collection. Data dependencies must precede their users and are limited
to depth 64. Groups/actions are checked exhaustively within a 64-million-step
validation budget. Additional group/reference payload is limited to four million
indices; transport lookups are limited to 10,000 descriptor visits. Invalid or
excessive input rejects before execution rather than triggering unbounded closure.

A union is one partial injection. Overlapping source rows must agree and distinct
sources must have distinct targets. It cannot resolve conflicting action records
or change their event identities. The core retains descriptors and reverse
indices; it does not retain expanded derived transports. Interpreter certificates
read the same shared lookups. No new predicate instruction, rule-closure mechanism,
continuous angle, or geometry is introduced.

[The standalone reference example](../examples/language/reference.pzl.json) uses
a C3 action to derive a position map. Load it with the existing native/WASM
`Puzzle` API and query `act`, `transport`, or normal permission planning.

The initial version-2 compressor used extra charts when correspondence
components outnumbered references. This is removed by version 3 below; legacy
version-2 documents remain supported without rewriting their canonical bytes.

## Compact format version 3

The source/package/definition versions must agree. All domains, predicates,
requests, profiles, choices, events, collections, groups, maps and bindings keep
their version-2 meanings. `compact` now uses these declarations:

| Field | Meaning |
| --- | --- |
| `spaces` | Finite coordinate-set sizes, including correspondence anchors A and placement references R |
| `positionMaps` | Sorted injective partial rows A → R with `sourceSpace` and `targetSpace`; or a compatible earlier `base` with input/output actions |
| `families` | `anchorSpace`, `referenceSpace`, and position-map IDs |
| `bindings` | A total injection from the declared reference space R into actual placements; no entries for A |

For position θ, `P_θ: A ⇀ R`. There is no restriction |A| ≤ |R|: only the
participating subset at each position must inject into R. Undefined anchors
may differ between positions. One correspondence can be present at positions
1 and 2 but absent at nominal position 0. The anchor coordinate does not imply
an identity position or a physically legal configuration.

Movement is unchanged:

`T(s→t) = B ∘ P_t ∘ inverse(P_s) ∘ inverse(B)`.

First unbind a source placement to R, invert P_s to find its anchor in A, then
apply P_t and bind its reference to the target placement. A row exists only
where this entire composition is defined. Defined self-loops still participate
and retain events. The core never stores angular state in anchors or creates
placements just to bind them.

A derived map is `output_action ∘ base ∘ inverse(input_action)`. Input acts
on the source space, output on the target space. Their group orders may differ.
Every family position and derived base must share the same exact source/target
space IDs; cardinality equality alone is insufficient for map compatibility.
Both spaces participate in authoring interning. Explicit reverse rows map R
back to A and are built once; derived maps retain only descriptors.

The compressor assigns one anchor per consistent connected component of the
(position, placement) correspondence graph. It creates a single family across
all declared positions, regardless of the anchor/reference size ratio. Unions
remain useful for distinct phase cases and separate supports. Sparse explicit
maps are retained when cheaper than the exclusive descriptor graph, including
reverse rows, groups, actions, bindings and both spaces.

[The version-3 example](../examples/language/anchors.pzl.json) uses five anchors
and three bound references, with independent C5/C3 input/output actions. The
older [square example](../examples/language/reference.pzl.json) remains a valid
version-2 input. Native/WASM queries expose both forward and inverse
`positionMap` lookups as well as `act`, `transport` and normal permission planning.

Validation retains the 1,000,000-point bound per space, four-million-entry
additional payload budget, 64-level dependency bound and bounded union
validation scratch/work. Input/output dimensions, injectivity, binding space,
family spaces, action laws and every dependency are checked before execution.
Lookups and interpreter certification share compact descriptors; endpoint
iteration does not create permanent expanded tables. No controller, geometric
path, predicate, interaction or puzzle rule changes are part of this format.

See [the refactor measurements](reference-anchors.md) for frozen-oracle parity,
Helicopter factoring and storage/lookup results.
