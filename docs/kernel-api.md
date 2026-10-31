# Portable kernel API

The C++ abstract core, compiler, session, and geometric interpreter form the reusable kernel. Native C++ applications and JavaScript/TypeScript applications using WebAssembly consume the same implementation. Renderers and controllers are clients: Three.js, React, Vite, Canvas, camera controls, input gestures, and animation clocks stay outside the kernel.

The current kernel version is `0.7.0`, public API version `7`, and frame-buffer version `1`. Geometry API and pose-buffer versions remain `1`. The runtime accepts package/definition formats 1, 2 and 3, the version-1 Gear factory wrapper, and session schema 2. Version-1 and version-2 definitions retain their original fingerprints; migrated compact packages receive new pins, so old saves require their matching old definitions. Native consumers rebuild against the new headers; the SDK and WASM runtime must be updated together.

The [puzzle language API](core-language.md), advertised as `languageAPIVersion = 3`, exposes the JSON language and domain-free executor through `Twisty::core`, `Twisty::language`, WASM `Puzzle`, and the TypeScript `Puzzle` adapter. Completion selection and bounded discovery are separate from permission checking. The cube/bandage authoring library is exposed as C++ `language::cube_module()`, WASM `cubeModuleJSON()`, and TypeScript `createCubeSource()`; `authoringLibraries` lists the supported models. `Twisty::symmetry` provides the shared standard-C++ finite-group utilities. Every shipped preset, including Bagua and custom Gear masks, uses the same core through an explicit package adapter. The pure program preview remains separate from the persisted definition format. Current session schemas refer to the standalone state schema; the retired finite-definition schema is not shipped.

## Compiler-independent runtime descriptors

`Twisty::runtime` links only `Twisty::core`. Include `twisty/runtime.hpp` for
`runtime::Profile`, `RequestBinding`, `Bindings`, `MovementBinding`, and
`CompiledPuzzle`, and `twisty/completion.hpp` for `runtime::bind_profile`,
`bind_default_profiles`, `discover`, and `rejection_inputs`. These headers and
the library require neither JSON nor the source compiler or geometric interpreter.
The [numeric runtime example](../examples/runtime/main.cpp) links this target in
an independently installed consumer.

Descriptors are owning C++ values, shared as const by an execution context.
`CompiledPuzzle` owns its validated `core::Program` and symbolic/completion data.
The compiler emits these descriptors; `language::Profile`, `RequestBinding`,
`Bindings`, and `CompiledPuzzle` remain aliases, and the `language::` completion
functions remain available through using declarations. Include
`twisty/authoring/compiler.hpp` explicitly when calling source compiler functions;
`completion.hpp` no longer includes it transitively. Rebuild native consumers
when updating libraries and headers together.

`Bindings::movements[slot][choice]` is a `MovementBinding` naming an action slice
through `first_action` and `action_count`. It includes an empty idle slice at
choice zero and preserves the exact per-move piece scope. It is not a dense
placement table. `runtime::movement_participates(puzzle, slot, choice, piece,
placement)` takes global core piece/placement indices and checks that scope and
the exact transport support. A defined self-loop participates. The query is
structural: it does not evaluate request permission or profile availability.
Unknown piece/placement indices use `core.input`; missing or out-of-range movement
bindings use `runtime.binding`. Queries own their results and expose no buffers.
Compiler-produced bindings have validated program references; manually assembled
descriptors must preserve those same relationships.

`ControlCatalog::compile(definition, bindings, controls)` resolves route profiles
from compiled `runtime::Bindings`. The source-taking overload remains a compiler
adapter. Piece-to-family queries inspect compiled movement/action scopes rather
than the canonical source AST, including explicit piece subsets and nondefault
route completions. Only actual dispatch plans test permission.

The movement slices are runtime metadata and are deliberately omitted from
existing inspection and definition serialization, preserving their canonical
bytes and digests. `CompiledPuzzle::source_version` remains the legacy inspection
encoding tag, not an executable rule. This extraction changes no WASM/TypeScript
method, API version, puzzle format, session format, or geometry/buffer layout.
Session/package loading still uses the compiler adapter; a source-free artifact
loader is a subsequent step. The full WASM module still includes authoring.

## API 7 correspondence anchors and placement references

A `PositionMap` is an injective partial map between two explicit finite spaces:
`source_space` (correspondence anchors A) and `target_space` (placement references
R). `ProgramData.position_maps` replaces `permutations`; a `TwistFamily` stores
`anchor_space`, `reference_space` and position-map IDs. Every position must have
those exact space IDs. Its `ReferenceBinding.space` must be the reference space R.
The two spaces may differ in size. Neither anchors nor reference indices are
piece identities or mutable state; only R has a placement binding. A nominal
anchor position need not cover A, be an identity map, or be a legal state.

Transport remains `B * P_target * inverse(P_source) * inverse(B)`. The inverse
of a position map returns an anchor index, not a placement reference. Derived
maps remain `output * base * inverse(input)`: input actions have size |A|,
output actions have size |R|, and base maps must match both space IDs. Actions
may belong to different finite groups. Dependencies remain acyclic, ordered and
bounded; only explicit rows and their reverse indices are retained. No dense
derived table is cached by the executor or interpreter.

```ts
puzzle.query({op: 'positionMap', map: 1, point: 3});
// {status: 'Evaluated', point: 0, defined: true}: anchor 3 -> reference 0
puzzle.query({op: 'positionMap', map: 1, point: 0, inverse: true});
// {status: 'Evaluated', point: 3, defined: true}: reference 0 -> anchor 3
```

Native clients use `Program::position_map(id, point, inverse=false)`. Bounds
follow the source space for forward queries and target space for inverse queries.
Undefined rows return null/empty; out-of-range inputs reject with `core.input`.
`Program::transport` still reports undefined rows as nonparticipation and defined
self-loops as participating actions with their original events.

Source/package/definition and preview version **3** introduce `positionMaps`,
`sourceSpace`/`targetSpace`, and family `anchorSpace`/`referenceSpace`. The compact
schema is version 2. The runtime also accepts the unchanged source versions 1
and 2. Version-2 square `permutations` are read with equal source/target spaces
and serialized back in their original shape. Old canonical fingerprints and
saved-session replay remain valid with the matching old definition. Migrated
packages have new pins; there is no automatic save conversion. Cube/Gear
factories still emit their supported version-2 square special case.

Rebuild C++ consumers for the renamed structs/fields, and update the SDK and
WASM together. `languageAPIVersion` is 3; `abstractCapabilities` advertises
`rectangular-position-maps`. Predicate version 1, session schema 2, controller
API 1, geometry API 1 and all binary buffer layouts remain unchanged. The
[rectangular example](../examples/language/anchors.pzl.json) has five anchors,
three references and correspondences absent at position zero.

## API 6 family controller

`Twisty::controller` is an optional portable library above session/execution.
`ControlCatalog` is immutable data shared by an execution context; `Controller`
holds that context and accepts explicit states or live sessions. It owns no UI,
clock, camera or additional logical state. `controllerAPIVersion` is `1`.

```ts
const session = new KernelSession(module, packageText);
const controller = new KernelController(module, session);
controller.catalog();
controller.familyState('UF'); // current: {UF: 'a'}, revision, definitionDigest
controller.familiesForPieces(['corner/FLU', 'corner/FRU']); // families: ['UF']
controller.destinations('UF', 0, 2); // paginated; maximum 32 destinations
const intent = {family: 'UF', targets: {UF: 'd'}};
controller.planTo(intent, session.native.revision()); // pure, returns Planned
controller.turnTo(intent, session.native.revision()); // one atomic history entry
controller.dispose();
session.dispose();
```

Matching WASM calls are `createController(session)`, `catalogJSON()`,
`familyStateJSON(session, family)`, `destinationsJSON(session, family, offset, limit)`,
`planToJSON(session, intentJSON, revision)` and `turnToJSON(...)`. Native typed
methods use `family_state`, `destinations`, `plan_to`, and `turn_to`. All JSON
strings/results are owned copies; no controller method exposes a borrowed buffer.
Dispose a controller before its frontend session wrapper. The native controller
retains its immutable execution context; a supplied live session must share that
exact context, including when created by `createSessionFromDefinition`.

Source stops come from placement readers. Requests, complete profile choices,
path IDs and preferred paths come from optional `catalog.controls` version 1.
The controller asks the existing permission relation to check the complete tuple
and verifies its target stops. It does not execute transports directly. Controls
are independent of optimized core reference-family IDs. Missing metadata yields
an empty control catalog, leaving existing request APIs available.
The public catalog contains driver/family descriptors; complete route authoring
records remain in the canonical definition's binding catalog and are not copied
to every frontend controller instance. Routes share compiled completion tuples.

Results distinguish `Ready`, `Planned`, `Committed`, `Blocked`, `NoDirectRoute`,
`NoOp`, `ChoiceRequired`, `Unavailable`, `StaleRevision`, and `Invalid`. A preview
never changes state/history. Explicit revisions are opaque decimal strings from
the session; stale plans cannot commit. The controller rechecks the route at
execution. `ChoiceRequired.paths` lists stable path IDs; supplying `pathId` selects
one. A current-stop intent is a no-op unless an explicit nontrivial path is given.
Paired families supply both targets and commit one simultaneous tuple.

The additive `piece-family-selection` controller capability provides
`familiesForPieces(pieceIds)` / WASM `familiesForPiecesJSON(session, piecesJSON)` /
native `families_for_pieces(stateOrSession, pieceIds)`. A `Matched` result contains
deduplicated persistent `pieces` and matching `families` in catalog order; session
queries also include `revision` and `definitionDigest`. Every selected piece must
participate in **one common route** from that family's current phase. Defined
self-loops count as participation. Matching uses the route's complete local-move
choices and compact transport lookups, without planning all destinations or
retaining expanded maps. It is structural: guards can still block a matching
family, so clients must use `planTo` / `turnTo` for legality and execution.

An empty selection returns all families; an incompatible selection returns an
empty list. Missing control metadata returns `Unavailable`; unknown pieces or
malformed input return `Invalid` through JSON wrappers (typed native methods
throw `DiagnosticError`). Inputs are limited to 4,096 piece IDs and 1 MiB of JSON.
Queries own their results and never change state, history or revision. Controller
API 1, puzzle formats, fingerprints and sessions are unchanged; the SDK returns
`Unavailable` when an older runtime lacks this optional method.

In the reference application, Helicopter and Melty piece clicks toggle a shared
multi-selection across views and the inspector. Matching families replace piece
destination ghosts: a unique family opens automatically; otherwise the user
chooses a matching family, then an angular destination. Selections follow piece
identities through moves and undo/redo. Click a selected piece again or remove its
chip to deselect it. Use Clear piece selection, click the background, or press
Escape to clear the whole selection.
Other puzzles retain their existing single-piece destination controls.

`Session::snapshot(SnapshotDetail::StateOnly)` / WASM `stateSnapshotJSON()` / SDK
`stateSnapshot()` omit `legalRequests` and `candidateRequests`. They retain state,
digests, revision, solvedness and history information. `lightweightResults(true)`
configures command results to use this form; explicit full `snapshot()` remains
available. Controller `turnTo` always returns a lightweight snapshot. Its JSON
result includes canonical `transitionRecords` for the existing animation API.
This configuration is presentation-only and is not included in saves or pins.

Helicopter now distinguishes six marked mechanism stops per grip. Physical rules
and goals project the stop pairs `(a,d)`, `(b,e)`, `(c,f)` onto their original
classes. Melty controls reuse existing center phases. These packages have new
definition fingerprints; older saves require their matching older definitions.

## Family motion and transport selectors

Realizations may declare `motionFamilies: {version: 1, families: {...}}`. A family
contains `axis`, `pivot`, `space: 'world' | 'local'`, and `coordinates`, mapping
finite stop IDs to radians. A path binding `{family, from, to, winding, rate?}`
generates the signed angle
`rate * (coordinates[to] - coordinates[from] + 2*pi*winding)`. It applies to the
actual source pose. `rate` defaults to 1; it is never inferred from endpoint
orientation. Fixed-axis subtraction is supported; noncommuting motion retains
explicit ordered steps. Bindings work in catalog `operationTracks`, articulated
`rotationTracks`, and inline paths. Declare `family-rotation-generators` as a
required capability. Signed path limits and endpoint/port certification still
apply, including pivot restrictions for spherical and planar realizations.

With `transport-path-selectors`, an articulated operation entry may be
`{kind: 'transport-regions', regions: [{request, path}, ...]}`. Each region uses
that request's shared transport support in the component's domain and a named
rotation path. Regions must cover exactly the operation's participants, agree
with its targets, and agree on full signed paths where they overlap. Validation
iterates placements without retaining another dense request/placement table.
Sampling resolves a participating source through the shared transport views.
Legacy explicit/shared path tables remain supported.

Optional `controlLayout` version 1 appears in the realization and scene descriptor.
Its `drivers[id].angles` arrays match authored stop order; optional `paths[pathId]`
records signed display angles by driver. These numbers describe the geometric
control diagram, not permission. Geometry API 1 and all binary buffer layouts are
unchanged. Unknown required geometric capabilities are rejected.

## API 5 placement actions and compact transports

`ProgramData` adds finite groups, finite actions, placement collections, reference
spaces, partial permutations, bindings, twist families, and compact transport
descriptors. Groups use identity element 0 and left multiplication. Actions are
regular, tabulated, or trivial. Reference actions use the same representation;
their group can be smaller than, or independent of, a placement group.

`Program::act(collection, element, globalPlacement)` returns a global placement.
`Program::transport(map, globalPlacement)` returns `optional<Index>`: an empty
result means no participation, while a returned source index is a participating
self-loop. Unknown indices throw `core.input`. These methods are pure and share
immutable data. Actions operate on placements, without asserting puzzle symmetry
or bypassing the permission relation.

The same queries are available through `Puzzle.query()` / `queryJSON()`:

```js
puzzle.query({ op: 'act', collection: 0, element: 1, placement: 0 });
// {status: 'Evaluated', placement: 1}
puzzle.query({ op: 'transport', map: 0, placement: 0 });
// {status: 'Evaluated', placement: 1, participates: true}
```

These two queries require no state. Their numeric IDs index the compact preview.
Version-2 previews use `program.compact`, `bindings.moveMaps`, and
`bindings.collections`; they omit expanded `transportMaps`. The reported
`transportStorageBytes` counts retained 32-bit transport payloads and reverse
indices, excluding allocator/container overhead, JSON metadata, and predicates.
It is deterministic across native and WASM and does not expand derived maps.
This inspection metric is omitted from canonical version-2 definitions and their fingerprints.
`bindings.moveMaps` uses `4294967295` for the implicit idle choice; it has no
transport and must not be passed to a transport query. Version-1 previews retain
their original shape and canonical bytes.

Source/package/definition version 2 introduces the compact data carrier described
in [the language guide](core-language.md#compact-format-version-2). Predicate
version 1 and session schema 2 remain unchanged. Stored completions and opaque
events still determine the full motion path, including winding. `Operation.maps`
and `Operation.roles` are now immutable shared lookup views with indexing and
`size()`, replacing dense vectors. Their definition owns the compiled program,
so they remain valid after the originating session is destroyed.

## API 4 naming

Update native includes, namespaces, and link targets, and rebuild C++ consumers. Update JavaScript/TypeScript clients and their WASM runtime together. The former names are removed rather than retained as aliases.

| API 3 | API 4 |
| --- | --- |
| `twisty/flat_core.hpp`, `twisty::flat`, `Twisty::flat_core` | `twisty/core.hpp`, `twisty::core`, `Twisty::core` |
| `twisty/relational_compiler.hpp`, `twisty::relational`, `Twisty::relational` | `twisty/authoring/compiler.hpp`, `twisty::language`, `Twisty::language` |
| `twisty/relational_completion.hpp` | `twisty/completion.hpp` |
| `twisty/relational_cube.hpp`, `twisty/relational_gear.hpp` | `twisty/authoring/cube.hpp`, `twisty/authoring/gear.hpp` |
| WASM `RelationalPuzzle`, SDK `ExperimentalRelationalPuzzle` | `Puzzle` |
| `compileRelationalJSON()` | `compilePuzzleJSON()` |
| `relationalCubeModuleJSON()`, `relationalGearModuleJSON()` | `cubeModuleJSON()`, `gearModuleJSON()` |
| `createRelationalCubeSource()`, `createRelationalGearSource()` | `createCubeSource()`, `createGearSource()` |
| `RelationalSource`, `RelationalPackage`, `RelationalDefinition` | `PuzzleSource`, `PuzzlePackage`, `PuzzleDefinition` |
| `RelationalPreview`, `RelationalQuery`, `RelationalResult` | `ProgramPreview`, `ProgramQuery`, `ProgramResult` |
| `experimentalAPIs.relational`, `relationalLibraries` | `languageAPIVersion`, `authoringLibraries` |
| `relationalDefinitionVersion`, `relationalCatalogKinds` | `definitionFormatVersion`, `catalogKinds` |

Core diagnostics use the `core.*` prefix. Serialized `relational-*` kinds, schema identifiers, and the `relational@1` rule-module identifier retain their format-1 names. These stored identifiers participate in semantic pins. JSON filenames omit the `relational` prefix: packages use `puzzle.pzl.json`, `bindings.json`, and `package.json`; Gear factory inputs use `source.json`. Renaming files changes no document contents, semantic pins, or schema-2 saves.

## Build and consume

```sh
npm run build:kernel
cmake --install build/native --prefix /your/kernel/install
```

This builds the C++ libraries and WASM runtime, and assembles the independent ES-module package in `build/kernel/`. It does not build or start the React/Three.js app. Nothing is published to a registry. Copy the entire `build/kernel/` directory to another project or install that local directory as an npm package. Its runtime has no external JavaScript dependencies. TypeScript declarations include both the convenient adapter and generated low-level bindings.

The native package exports `Twisty::core`, `Twisty::runtime`, `Twisty::language`, `Twisty::execution`, `Twisty::symmetry`, `Twisty::session`, `Twisty::controller`, `Twisty::geometry`, `Twisty::catalog`, and aggregate `Twisty::kernel` targets. A consumer can use:

```cmake
find_package(TwistyKernel 0.7 CONFIG REQUIRED)
target_link_libraries(my_frontend PRIVATE Twisty::kernel)
```

Configure that consumer with `-DCMAKE_PREFIX_PATH=/your/kernel/install`. The package includes public headers and its JSON dependency. A headless consumer may link `Twisty::session` without geometry. Both bindings are checked on the current Linux ARM64 container; native packages should be built for the destination platform and compiler.

```cpp
#include <twisty/kernel.hpp>

twisty::Session session(definition_json);
twisty::GeometryDefinition geometry(session.execution(), realization_json);
auto result = session.execute("U", session.revision());
for (const auto &transition : result.at("transitions")) {
  const auto animation = geometry.animate(transition.dump());
  const auto poses = animation.evaluate(0.5);
  const auto drawing = geometry.project(poses);
  // Draw drawing.positions(), normals(), indices(), and transforms().
}
```

JavaScript clients import the package's public entry point:

```js
import { createKernel, KernelSession, GeometryInstance } from '@twisty/kernel';

const kernel = await createKernel({ wasmURL: '/assets/twisty.wasm' });
const session = new KernelSession(kernel, definitionText);
const view = new GeometryInstance(kernel, session.native, realizationText);

const source = session.snapshot();
const request = { operation: 'U', parameters: {} };
const preview = session.plan(request); // Pure; does not change session/history.
if (preview.status === 'Legal') {
  const result = session.execute(request, source.revision);
  for (const record of result.transitionRecords ?? []) {
    view.prepare(record);
    view.evaluate(0.5); // Linear progress; the controller may apply easing.
    const matrices = view.matrices; // Owned JS copy; no borrowed WASM view.
    // Draw using your renderer, then evaluate until progress reaches 1.
  }
}
view.dispose();
session.dispose();
```

Supply `wasmURL` when a bundler relocates assets. Without it, `createKernel()` resolves the binary beside its generated ES module, which supports static browser hosting and Node file loading. `locateFile(filename, directory)` is also available for custom asset routing. There are no DOM, network-fetch, event-loop, or framework requirements in session or geometry operations; the module loader handles the chosen deployment environment.

## Abstract and controller APIs

Use `Session::execution()->plan()/decode()/is_solved()` for native logical queries. `compile_execution_json()` / WASM `compileExecutionJSON()` accept an explicit relational package, persisted relational definition, or `relational-gear-prototypes` wrapper. Unsupported kinds return `Invalid` with `definition.unsupported`; they are never reinterpreted. `core.hpp` declares the current `twisty::core::Program`; it does not restore the retired free planner/compiler functions. The old root-level `compiler.hpp` and WASM `compileJSON()` remain removed. `Session::definition()` returns symbolic interpreter/inspector metadata declared in `catalog.hpp`; it contains no executable guards. Construct geometry with `session.execution()` or serialized `session.definition_json()`. The metadata-only geometry constructor is removed.

| Task | C++ | WASM / TypeScript |
| --- | --- | --- |
| Discover versions and capabilities | `kernel_info()` | `kernelInfoJSON()` |
| Compile/validate a definition | `compile_execution_json()` | `compileExecutionJSON(source)`; `new Session(source)` |
| Read definition and exact state | `definition()`, `state()`, `definition_json()`, `state_json()` | `definitionJSON()`, `stateJSON()`; adapter `definitionText`, `stateText()` |
| Inspect revision, goal, legal requests, history | `snapshot()`, `revision()` | `snapshotJSON()`, `revision()`; adapter `snapshot()` |
| Validate an assignment | `execution()->decode()/validate()` | `validateStateJSON(stateText)`; adapter `validateState(stateText)` |
| Plan without committing | `execution()->plan(state, operation, choices?)` | `planJSON(requestText, stateText)`; adapter `plan(request, stateText?)` |
| Commit a primitive | `execute(operation, expectedRevision)` | `executeJSON(requestText, expectedRevision)`; adapter `execute(request, expectedRevision?)` |
| Algorithms and seeded legal walks | `run()`, `scramble()` | `runJSON()`, `scrambleJSON()`; adapter `run()`, `scramble()` |
| Linear history | `undo()`, `redo()` | `undoJSON()`, `redoJSON()`; adapter `undo()`, `redo()` |
| Persistence and checked replay | `save()`, `load()` | `saveJSON()`, `saveWithPresentationJSON()`, `loadJSON()`; adapter `save(presentation?)`, `load(document)` |

Requests contain a directed operation ID and `parameters: {}` for the currently registered primitives. Planning returns `Legal` with an immutable transition and a lossless `transitionRecord`, or `Blocked` with structured evidence. JSON wrappers return `Invalid` and diagnostics for invalid input; constructors and native typed APIs signal errors with exceptions. Assignment validation returns `Valid` or `Invalid`; validity does not establish reachability.

All abstract predicates are compiled to the domain-free program's membership and count instructions. There are no special piece, mechanism, footprint, or placement-count guard handlers in the shipped runtime. Domains, cells, roles, and movement tables in the grouped catalog certify interpreter tracks and support inspection; they do not decide move legality. See [the relational language](core-language.md) for authoring interactions.

Only session commands commit state. Successful commands return `Committed` with ordered transition records and a snapshot. A stale expected revision returns `StaleRevision`; blocked or stale commands preserve state and history. Algorithms use `interactive` (keep the legal prefix) or `transactional` (commit all or none) policies. Adapter command methods default to the current revision for synchronous controllers. Controllers that plan asynchronously should capture a revision and pass it explicitly.

Call each session and geometric instance sequentially on its owning thread. The controller owns request queues and scheduling; the kernel does not synchronize concurrent calls to one mutable handle. Separate worker-owned instances can exchange immutable serialized records through host-provided orchestration.

To create another session for the same definition, use `Session(existing.execution())` in C++, `createSessionFromDefinition(existing)` in raw WASM, or `new KernelSession(module, existingSession)` in TypeScript. This shares immutable compiled tables and starts at the definition's initial state with empty history and revision zero. Mutable state/history remain independent; handles may be disposed in any order. WASM sessions must belong to the same runtime. Discover this additive feature in `sessionFeatures`, which includes `shared-definitions`. Sharing avoids recompiling large catalogs such as Bagua; use `load()` separately when replaying a saved history.

The supplied WASM build starts with a 32 MiB heap and can grow to 512 MiB. The 672-piece 24-cell requires the larger ceiling; the initial allocation stays the same for smaller puzzles. Heap growth can invalidate borrowed typed views, so copy them before another kernel call. Definition hashing reads its input without making a second padded document copy.

Execution source packages and persisted definition inputs are limited to 16 MiB; language source text is also limited to 16 MiB. The CLI emits compact JSON. A large compiled definition can exceed the input bound even when its source package fits: reload the original package or share the already compiled execution context in that case. Reference app and CLI session imports retain their separate 4 MiB limit.

The pure plan API accepts a separate source-state document for previews and analysis. Its state digests identify the source; that preview does not grant permission to commit against a newer revision. The session always replans the submitted request against its authoritative state.

Keep `stateText()`, `definitionText`, `transitionRecord`/`transitionRecords`, and native session saves as opaque strings when transporting exact logical data. Parsing JSON into ordinary JavaScript numbers can lose integers above `2^53 − 1`. Parsed objects are convenient for UI inspection; forward the original record strings into geometry and persistence APIs.

## Sessions and checked replay

Cube, compound-bandage, Helicopter, Bagua, and Gear-family presets load `packages/<preset>/package.json`: `{kind: "relational-package", formatVersion: 1, source: <language AST>, catalog: <port/event bindings>}`. The session/frame adapter accepts `relational-cube-bindings` version 1 with constant profiles, and `relational-port-bindings` version 1 with state-dependent profile availability. These uniform catalogs require reciprocal named moves and uniform event tracks. Gear uses `relational-articulated-bindings` version 1 with explicit local inverses and per-domain event certificates; the interpreter dispatches tracks per participant. `catalogKinds` advertises these catalog formats. The raw relational API remains available for arbitrary finite relations without these interpreter-adapter requirements. Helicopter and Bagua sources are offline-authored ASTs rather than an additional built-in C++ authoring factory.

`Session` owns the controller history. Its `Execution` context always owns a compiled relational program. No historical planner, occupancy validator, or home-goal test is linked into the runtime. A grouped, symbolic state document (`placementOf`, empty `mechanism`) remains a compatibility view for the inspector and existing frame tables. The adapter maps it to global flat placements before every permission, invariant, or goal query. Domains and resource/port incidence in this view are binding metadata, not new flat-core fields.

A relational request optionally supplies `choices: string[]` in compiler `bindings.choiceSlots` order, including every idle value. Omitted choices invoke the declared default binder outside the core. Supplying choices bypasses that binder and checks precisely the supplied tuple. `snapshot.candidateRequests` carries bound defaults before permission checks; a profile whose `when` is false is omitted, and an omitted-choice request then returns `ChoiceRequired`. Source-phase requirements remain part of the relation even when choices are supplied explicitly. `snapshot.legalRequests` carries the accepted default completions, and previews/destination controllers retain them. This list concerns those declared candidates; it is not exhaustive discovery for a relation with other permitted tuples. The pure experimental API provides explicit bounded discovery.

Transition records include `witnessVersion: 1`, `request.choices`, and an opaque `event` on every `pieceActions` entry. Self-loops remain actions. The geometric interpreter requires the completed tuple, replans it through the core executor, and checks the entire record, state hashes, and events before accepting it. The current uniform frame adapters accept their declared face/stop-rotation event; it rejects other per-piece events rather than guessing their animation. Rejected records preserve the current frame.

A forbidden candidate reports `Blocked` and `scope: "completed-request"`, with `relation.rejected`, `state.target_invariant`, or `transport.conflicting_writes`. For relation rejection, `evidence.kind: "relation-proof-inputs"` and `implicatedPieces` identify state inputs in a sufficient Boolean proof of rejection for that supplied tuple. The completion helper follows failed count bounds through the existing evaluator trace, skipping inactive conditional branches. This is not a minimal cause or proof that each listed piece independently blocks the move, nor a proof that other completions are impossible. Non-relation rejections have no relation proof inputs. An unknown/malformed tuple returns `Invalid`. Rich source-level explanations remain future work.

Persisted `relational-definition` format 1 includes predicate version 1, the source/catalog, compiled program and symbolic bindings, and the grouped interpreter metadata. SHA-256 covers its canonical compact, key-sorted JSON body, excluding only `definitionDigest`; named profile/default policies and inverse metadata are covered. Loading recompiles the source and checks the entire resulting definition, not just its claimed hash. Equivalent but differently authored Boolean formulas need not share a digest. Realizations keep schema 1 with the verified semantic pin and preserve existing meshes, parameters, and realization IDs. Generation compiles and validates current relational candidates before writing those pins; package reproducibility and current native/WASM checks verify the resulting artifacts.

Saves use [session schema 2](../schemas/session-v2.schema.json). Each history entry records `choices`, including its redo tail. Redo and replay validate those recorded choices rather than selecting a default again; undo maps the recorded local moves to their declared inverses and checks that the complete prior state is restored. The saved tuple and semantic pin also determine the event witness. Corrupt completions, checkpoints, or digests reject the whole load without changing the live session. Schema-1 histories are rejected with `session.schema`; the live session is unchanged.

The legacy save-conversion methods are removed from C++, WASM, and TypeScript. Use a previous release if an old schema-1 save must be converted before upgrading. The historical evaluator and its comparison tools have also been removed from the repository. Static historical JSON catalogs remain only as offline geometry inputs and archived review data; they are neither executable nor accepted by the runtime. `BUILD_TESTING` now controls only tests of the current kernel and its independent consumers.

`relational-gear-prototypes` format 1 accepts `puzzleId` and a distinct `gearedEdges` subset. Native `language::gear_module()` / `gear_module_source()`, WASM `gearModuleJSON(sourceText)`, and SDK `createGearSource(kernel, source)` emit the standard AST and articulated catalog; the factory is advertised as `gear-family`. The [wrapper schema](../schemas/gear-source.schema.json) documents the finite input. `compileExecutionJSON()` and `Session` accept the wrapper directly. The old `gear-cube-prototypes` kind is unsupported by the runtime.

Articulated catalogs supply `events[event] = {domain, request}` and reciprocal `moveInverses[domain]` tables. Every event must identify exactly the local move selected in that request's default profile. Certificate profiles choose concrete local moves in every slot. The interpreter statically verifies the corresponding per-component transport tracks and dispatches them per recorded participant at animation time. A completed tuple can therefore combine different local movements while retaining their original winding and world/local pivots. Explicit idle choices produce no participation. All data and buffers retain their existing ownership and versions. Native `Execution::event_operation(piece, event)` resolves a catalog event using the piece's index in the grouped inspector definition; it is not a permission query. See [the Gear migration](core-language.md#gear-family-migration).

## Geometric and renderer APIs

The primary API compiles a realization once into an immutable `GeometryDefinition`. All existing realization formats compile into shared pose tables, per-part bindings, rotation paths, and optional linear translation paths. Backend selection and cube-specific construction live in the private realization compiler, rather than the evaluator. No domain or puzzle logic is added to the abstract core.

| Task | C++ | WASM |
| --- | --- | --- |
| Compile a realization | `GeometryDefinition(execution, realizationText)` | `createGeometryDefinition(session, realizationText)` |
| Inspect assets and bindings | `scene_json()`, `positions()`, `normals()`, `indices()` | `sceneJSON()`, same buffer names |
| Inspect the space model | `space_json()` | `spaceJSON()` |
| Retain intrinsic prototypes (S³) | `intrinsic_mesh()` | `intrinsicMesh()` |
| Evaluate a resting state | `definition.frame(stateText)` → `GeometryFrame` | same |
| Validate and prepare an exact witness | `definition.animate(record)` → `GeometryAnimation` | same |
| Evaluate motion at explicit progress | `animation.evaluate(progress)` → `GeometryFrame` | same |
| Inspect timing guidance | `animation.duration_hint()` | `animation.durationHint()` |
| Read intrinsic poses | `frame.isometries()` | same |
| Project a frame for drawing | `definition.project(frame, "embedding")` → `GeometryDrawing` | same; projection argument required |
| Read projected buffers | `drawing.positions()`, `normals()`, `indices()`, `transforms()` | same |
| Resolve static part identity | `definition.resolve_part_json(id)` | `definition.resolvePartJSON(id)` |

`animate` verifies the full exact abstract witness, including completed choices, participant events and source/target states. It resolves each participant's event to a certified path, including participants whose placements do not change. Angles retain sign and full winding, so a Gear edge's 300° travel is preserved. It never commits logical state. Missing choices or a forged witness fail with `realization.witness`; definition mismatches use `realization.digest_mismatch`. The primary native/WASM operations throw on failure, as the existing constructors and state import do; native `diagnostic_result(exception)` provides structured diagnostics. The compatibility preparation method below still returns a diagnostic JSON result.

`animation.evaluate(t)` returns a new owned pose frame and changes neither the definition nor any previous frame or animation. Progress is linear and clamps to `[0,1]`; nonfinite values fail with `animation.progress`. Exact endpoint frames come from the placement catalog. Intermediate quotient motions use continuous covering-space lifts, with exact endpoint representatives as before. Ordered local/world rotation compositions remain ordered and share the supplied progress. Callers choose the clock and easing, for example `t*t*(3-2*t)`. There is no hidden `sample()`/`transforms()` dependency in this API. Multiple animations can be evaluated or sought independently.

The scene's `space` descriptor (also returned by `spaceJSON`) declares intrinsic and embedding dimensions, coordinate model, metric/signature, curvature/radius, mesh coordinates, use of homogeneous coordinates, matrix shape/order and scalar type. Pose buffer version 1 packs one column-major 4×4 **Float64 isometry** per visual part, in scene order. Current models are Euclidean homogeneous R³, planar E² with proper three-dimensional lifts, S² rotations in R³, and genuine SO(4) rotations for S³ in R⁴. Sphere mesh coordinates include radial display shells for stickers and internal parts; those shells are not additional intrinsic dimensions. Local mesh scale is published as `visualParts[i].meshScale` (default `1`), applied before the pose, and never included in an intrinsic isometry. Clients implementing their own projection therefore have the same local scale information as the built-in adapter.

The available `projections` are `"embedding"` for the existing R³/S²/E² models and `"stereographic"` for S³. The scene declares `drawingSpace: "R3"` and `drawingBufferVersion: 1`. Its `GeometryDrawing` uses the existing Float32 positions/normals, Uint32 local triangle indices, and Float32 column-major affine model matrices (drawing buffer version 1). Projection applies local mesh scale to the intrinsic poses; drawing matrices already include that scale, so renderers must not apply it a second time. The output includes its own scene and asset accessors so a later projection can supply new vertex/normal data rather than pretending that every projection is an object matrix. It owns matrices and either shares immutable embedding assets or owns deformed stereographic meshes. A frame from another independently compiled definition fails with `geometry.frame_mismatch`; copies of the same native definition share identity. Unknown projections fail with `geometry.projection`.

S³ support is described below. H³ hyperboloid geometry remains unimplemented. Interpret matrices using their declared model rather than assuming affine Three.js transforms. Euclidean checks remain specific to their authoring formats. Cameras, visible quotient copies, gestures and move selection belong to clients; S³ projection clipping belongs to the portable interpreter. Part resolution returns only persistent `pieceId` and optional `portId`, or `NoTarget`.

Native definitions, animations, frames and drawings share immutable compiled storage through reference counting. Any can outlive the session or the source definition handle. Native buffer references remain valid for the lifetime of their owning object. Each WASM result is a separately owned handle: call `.delete()` on animations, frames, drawings and definitions when finished. Typed arrays returned by WASM are borrowed read-only views and must be copied before another WASM call or deleting their owner; use `new Float64Array(frame.isometries())` for poses and `copyFloatView`/`copyIndexView` for drawing buffers. Never keep a borrowed view across possible WASM memory growth.

```js
const definition = kernel.createGeometryDefinition(session.native, realizationText);
if (!definition) throw new Error('Cannot compile realization');
const animation = definition.animate(exactRecord);
try {
  const frame = animation.evaluate(0.25);
  try {
    const drawing = definition.project(frame, 'embedding');
    try { const matrices = copyFloatView(drawing.transforms()); /* draw */ }
    finally { drawing.delete(); }
  } finally { frame.delete(); }
} finally { animation.delete(); definition.delete(); }
```

The optional TypeScript `GeometryInstance` helper manages these temporary handles and maintains an owned JS `matrices` array. It exposes `scene`, `definition`, `setState(stateText)`, `prepare(record)`, `evaluate(t)`, `destination(record)`, `resolvePart(id)`, and `dispose()`. `destination` evaluates an independent animation using the same assets/tables and leaves the live frame unchanged. Invalid state imports or preparations preserve its current frame and animation. The reference Three.js views and standalone Canvas example use this API.

Existing C++ `Geometry` (now an alias of `GeometryInstance`) and WASM `Geometry` remain compatibility facades. The string constructors and `createGeometry(session, realizationText)` still work. Native `GeometryInstance(sharedDefinition)` and WASM `createGeometryInstance(definition)` create independent compatibility instances sharing compiled storage. Their `setStateJSON`, `prepareAnimationJSON`, `sample`, `transforms`, and `bindHitJSON` behavior is preserved: `sample` applies the original smoothstep easing, and legacy hit bindings include candidate requests. Preparation returns `Prepared` with `durationHint`, or `UnsupportedTransition` with diagnostics, and leaves the previous presentation unchanged on rejection. Scene and buffer access retain their existing names. The new space/projection fields and API capability markers are additive; puzzle documents, semantic digests, and sessions are unchanged.

The public `SceneDescriptor`, `MeshAsset`, `VisualPart`, and `HitBinding` TypeScript types describe the wire data. Check `requiredCapabilities` against your renderer before constructing drawing resources. Current required drawing capabilities are `triangle-meshes`, `rigid-transforms`, `quotient-surface`, and `quotient-orbifold`. The kernel also advertises its shared-definition, pose and animation API features. The last capability requires support for cone stabilizers and their deck rotations, including the 2222 half-turn and 333 third-turn. The `diagram` flag identifies flat port diagrams. Material bindings are symbolic labels; the renderer chooses colors, lighting, fonts, and selection/blocking styles.

The additive `flat-quotient` realization exposes `ambientSpace: "E2"` and a `FlatSurface` scene descriptor with `topology`, `fundamentalBounds`, two column-major `deckGenerators`, and `twoSided: true`; optional `cutRadius` and `diskCenters` describe the cuts. It retains API 4, realization schema 1 and buffer version 1. The interpreter emits intrinsic physical parts and continuous lifted rotation frames; displayed copies, reflections, clipping and picking belong to the renderer. Renderers advertising `quotient-surface` must support those operations and two-sided planar assets. Native and WASM constructors and borrowed-view lifetimes are unchanged. See [flat surfaces](flat-surfaces.md) for the exact matrix conventions, square/tiled presentation, preset construction and reproduction.

Authoring may add `flatSurface.chart: "rhombus-60"` (the default is `"square"`).
The scene then includes `chart`, `fundamentalPolygon` (counterclockwise `[x,y]`
vertices), `latticeBasis: [a,b]` (Cartesian side vectors), and optional
`tileReflection` (a column-major proper 4×4 frame). For integer tile `(i,j)`,
compose translation `i*a+j*b` with `tileReflection` when `i+j` is odd. Without
that field all tiles are translated. Polygon clipping and point tests are
required; `fundamentalBounds` is its enclosing box, not its boundary. The
rhombus Klein bottle's two deck generators are a translation `T` and glide
reflection `G`, with `G T G⁻¹=T⁻¹`; their normal-form exponents are distinct
from lattice tile coordinates. Invalid chart names, nonplanar/out-of-chart
frames and nonembedded cut radii produce `realization.flat_surface` or
`realization.articulated` diagnostics. This additive metadata retains API 4,
schema 1 and buffer 1, with unchanged ownership and borrowed-view lifetimes.

The additive `topology: "pillowcase"`, `chart: "pillowcase-square"` requires
`quotient-orbifold` in addition to `quotient-surface`. Here `sideLength=L` is
the covering torus translation period. The scene's fundamental polygon is
`[(L/2,0),(L,L/2),(L/2,L),(0,L/2)]`, of area `L²/2`. Its three
`deckGenerators` are translations `(L,0)` and `(0,L)`, followed by the
half-turn `I(p)=(L/2,L/2)-p`. These matrices and their compositions act on
positions and orientations. The four `conePoints` are `(L/4,L/4)`,
`(3L/4,L/4)`, `(L/4,3L/4)`, and `(3L/4,3L/4)`; each has angle π.

The pillowcase scene also supplies `latticeBasis=[(L/2,L/2),(-L/2,L/2)]`,
`chartOrigin=(L/2,0)`, and `tileHolonomy`, the column-major proper lift of
`p -> (L,L)-p`. A tile image is `Translation(i*a+j*b)` composed with
`tileHolonomy` for odd `i+j`, or just the translation for even `i+j`.
`chartOrigin` locates the first polygon vertex; it is used for tile address
and polygon calculations, not as an extra matrix translation. The odd-tile
composition belongs to the deck group; `tileHolonomy` alone need not.
All images preserve handedness. Prefer `fundamentalPolygon` to its bounds
for clipping and picking. When present, `tileHolonomy` takes precedence over
`tileReflection`.

The interpreter emits each physical part once, including a half-domain mesh
for a piece containing a cone point. Applying all image matrices fills its
complete planar lift without coincident full meshes. Frames use the closed
diamond, resolving boundary and stabilizer ambiguity by lexicographically
comparing `(x,y,m[0],m[1],m[4],m[5])` rounded at `1e-9`. Sampling retains a
continuous lift until the exact canonical endpoint. Cone disks require
`cutRadius < L/2`; a declared ordinary-center disk tightens the bound to its
distance to the nearest cone, equal to `L/√8` at E and F. Invalid radii or incompatible chart names fail construction with
`realization.flat_surface`. API 4, realization schema 1, and buffer version 1
are unchanged; no ownership or constructor changes are introduced.

The additive `topology: "orbifold-333"`, `chart: "orbifold-333-rhombus"` also
requires `quotient-orbifold`. Here `sideLength=L` is the distance between
nearest cone points. Set `a=(L,0)` and `b=(L/2,sqrt(3)*L/2)`. The fundamental
polygon is `[0,a,a+b,b]`, with bounds `[0,0,3L/2,sqrt(3)*L/2]` and
`latticeBasis=[a,b]`. The three `conePoints` are `[0,a,b]`; `a+b` is another
representative of the first cone. Each cone angle is `2*pi/3`.

Its three `deckGenerators` are the translations `T1=2a-b`, `T2=a+b`, followed
by the positive 120° rotation R about the origin. The matrices are proper,
column-major 4×4 lifts with z unchanged. A displayed image is
`T1^i * T2^j * R^k`, for every integer i,j and k=0,1,2. In this topology,
`latticeBasis` describes the fundamental rhombus, **not** the translation
subgroup: image addresses use T1,T2. There is no `tileHolonomy` or reflection.
Renderers must enumerate all three rotation cosets, including when panning.
The interpreter emits each physical part once; a cone-center asset occupies
one 120° sector, so the three stabilizer images complete a full planar lift
without overlapping interiors.

Frames use the closed rhombus. Resolve equivalent deck images by comparing
`(x,y,m[0],m[1],m[4],m[5])` rounded at `1e-9`, as for the pillowcase.
Continuous sampling retains its lift until the exact canonical endpoint.
Cone disks require `cutRadius < sqrt(3)*L/2`. Ordinary disks additionally
avoid all cone lifts and have radius less than half the nearest distinct
same-center deck-image distance; for the three edge midpoints this gives
`cutRadius < sqrt(3)*L/4`. Invalid centers, radii, chart names, or missing
orbifold capability fail construction. This extends scene metadata under
API 4, realization schema 1, and buffer version 1; ownership, constructors,
and buffer layouts are unchanged.

The three-disk 333 package uses plain nearest-center material labels `A`, `B`,
and `C`. The six-disk package splits its hue patches along the two triangular
faces, with labels `A-light`, `A-dark`, through `F-light`, `F-dark`. Renderers
choose the two shades of each hue. These are ordinary fixed local port patches
that move with their pieces, including centers; no position-dependent
recoloring or additional rendering capability is required.

`kernelInfoJSON()` also advertises the `cube-spherical` realization. Its optional scene metadata identifies `ambientSpace: "S2"`, the six `diskCenters`, `sphereRadius`, `diskAngleDegrees`, `fidelity`, and `surfaceTransportVerified`. Mesh assets may declare `rotationSymmetryOrder`; featureless spherical centers use order four. These additive descriptors use the same buffer format and rendering capabilities as other realizations. Custom clients pass a compatible `cube-spherical.json` into the existing geometry constructor; the packaged example data includes both reference cube realizations. See [the spherical construction](spherical-cube.md) for its geometric verification and surface-model scope.

`polyhedral-spherical` intersects catalog prototypes with the unit sphere and uses the existing catalog frames and rotation tracks. The Helicopter package provides twelve 45° disks, seven shared assets, 44 existing pieces, and 48 ports. Local mesh vertices lie on unit shells; the embedding drawing matrix includes the uniform display scale, equal to `sphereRadius`, while the intrinsic pose remains an isometry. It retains the abstract model's volume, phase, catalog, and exclusion guards; a spherical outline can therefore show tangency for a request blocked by a volume-derived rule. See [the spherical Helicopter construction](spherical-helicopter.md). The copied example data includes `helicopter/helicopter-spherical.json`.

Polyhedral model `ports` map each local port ID to 3–32 distinct model vertex indices. The indices may be unordered; they must describe a coplanar, strictly convex polygon on an exterior supporting plane. The interpreter orders its boundary, triangulates one shared mesh asset, and uses the arithmetic mean of its vertices as the port anchor. Malformed polygons produce `realization.catalog` diagnostics. The experimental [Bagua model](bagua-model.md) demonstrates 146 pieces and 198 polygonal ports using the existing session/planning APIs and Euclidean, diagram and spherical documents.

`PolyhedralModel.surfaceVertices` is an optional array of 4–32 finite numeric triples, consumed by `polyhedral-spherical` only. It specifies the convex hull intersected with the **unit sphere**, in the same local frame as the original model. Omission falls back to `vertices`, preserving existing packages. Original `vertices` and `ports` continue to define port normals, identity and catalog checks; port indices never address `surfaceVertices`. Every declared symmetry must preserve both vertex sets and each labeled port. Malformed surface arrays and incompatible symmetries produce `realization.catalog`; degenerate sections, incompatible disk angles and failed continuous participant/stationary membership produce `realization.spherical`. Every active surface cut must have absolute unit-normal offset `cos(diskAngleDegrees)`.

The [spherical Bagua package](spherical-bagua.md) uses this extension to supply six approximately 66.11° disks and twenty shared assets while retaining all 146 pieces and 198 ports. Its copied document is `examples/canvas/data/bagua/bagua-spherical.json`. These additive schema/type fields originally shipped in API 1 and retain **buffer version 1**, with unchanged ownership, constructor signatures, capability names, indexed triangles and frame layouts. No Bagua-specific renderer API is required.

Drawing buffer version 1 (`GeometryDrawing` and the compatibility facade):

- Positions and normals are packed `Float32` triples. `positionOffset` is an offset in scalar elements, and `vertexCount` counts vertices. Position and normal slices have the same offsets and lengths.
- Indices are packed `Uint32`. `indexOffset` and `indexCount` count index elements. Indices are local to the corresponding asset's vertex slice.
- Matrices are column-major `Float32` 4×4 values. Matrix `i` occupies elements `16*i` through `16*i+15` and corresponds to `visualParts[i]`.
- Stable scene, piece, port, visual-part, and asset IDs connect logical and visual records. Dense array order is local to that scene, not a persistent puzzle identity.

Native buffer references and WASM typed views are borrowed. Copy exported WASM views immediately before another kernel call can invalidate their backing memory. Use `copyFloatView()` / `copyIndexView()` for assets and reusable destination arrays for sampled transforms. Do not serialize frames as JSON or keep heap views as application-owned storage. Explicitly call `delete()` on geometry handles and `dispose()` on adapter sessions; dispose front-end resources separately.

The SDK helper `GeometryInstance.destination(record)` caches verified endpoints per immutable geometry definition. It retains at most 64 records and approximately 2 MiB of record text plus owned matrix buffers; it never caches borrowed views or native animation handles. Each call returns an independent `Float32Array`, including cache hits, and altered records still undergo full witness verification. Disposal clears the cache. Controllers that already obtained a legal plan may instead project its target with `GeometryDefinition.frame()` and `project()` for endpoint ghosts, without constructing an animation or changing live geometry; execution still submits the original revision-checked request.

The renderer performs hit testing. Use `resolvePartJSON` to obtain a persistent piece and optional port. Only the compatibility `bindHitJSON` also returns candidate requests; those candidates still need abstract planning. In the current interaction contract, bind puzzle picks at resting frames and disable state-dependent picking during animation. The controller owns displayed-frame tags, selections, queues, and any synchronized views; camera movement is independent of logical state.

## Independent consumers and acceptance

`examples/native/` is a separate CMake project that uses only an installed `TwistyKernel` package. `examples/canvas/` is a custom Canvas 2D renderer and DOM controller that uses only the assembled kernel package. It demonstrates reusable assets, C++ frame sampling, port picking, blocked requests, algorithms, and undo/redo with cube, bandaged, Helicopter and experimental Bagua definitions.

```sh
npm run check:kernel
# Or serve the standalone package, without the reference web application:
python3 -m http.server 8080 --directory build/kernel
```

Open `/examples/canvas/` on that server. The ordinary production build also includes the example at `/kernel/examples/canvas/` for browser checks. `check:kernel` installs the native libraries, builds an external consumer, copies the WASM package to a temporary location without repository imports, and compares states, scenes, and endpoints for all four puzzles. Browser checks exercise the independent renderer/controller and verify that it loads only kernel-package modules.

Further portability work is deferred in favor of the original puzzle and realization roadmap. A renderer-neutral adapter conformance suite, broader destination-platform CI, and additional language bindings remain possible later steps. A common C ABI, package-registry publication, worker orchestration, and dynamic native plugin loading are separate deliverables. The current portable API already supports custom native and browser renderers/controllers without changing the puzzle implementation.

## Articulated realizations and gear authoring

`kernelInfoJSON().realizationKinds` advertises `articulated-euclidean`, `articulated-port-diagram`, and `articulated-spherical`. These additive realization capabilities originally shipped in API 1; their document schema and frame-buffer version remain 1. Clients discover supported kinds before loading documents. Existing kinds and semantic digests are unchanged. The authoring/compiler path accepts the `relational-gear-prototypes` wrapper described above and emits a geometry-free package. See [the Gear specification](gear-model.md) for its coupling policy.

An articulated document is pinned by `compatibleDefinitionDigest` and has:

- `assets`: a map of IDs to numeric `vertices` triples and integer `triangles` triples. Indexed meshes may be non-convex. C++ expands triangles into the same borrowed position/normal/index buffers as existing realizations; outward winding is authored, and normals default to flat per-triangle values. Each asset has 3–65,536 vertices and 1–262,144 triangles; at most 256 assets, 262,144 combined vertices, and 524,288 combined triangles are accepted.
- `componentsByDomain`: exactly every placement domain, each with 1–32 component records. Each component supplies `id`, `meshAssetId`, `role` (`body` or `port`), `placementFrames`, and `operationTracks`. Port components additionally supply `portId`; every local port is bound at least once. Several patches may share a port ID and have different frames/tracks; their unique component IDs produce distinct visual-part IDs, while color labels and hit bindings remain shared. Optional body `materialBindingId` selects appearance. Optional body-only `pieceIds` filters instantiation to a nonempty list of distinct existing pieces in that domain; port components cannot use this filter, so port coverage remains complete. Frames/tracks still cover the whole domain. Optional `symmetries` are at most 32 proper 4x4 label-preserving mesh symmetries, verified against vertices and oriented mesh boundaries/normals. Declaring them requires at most 4,096 asset vertices and 16,384 boundary edges.
- `placementFrames`: exactly every placement key, each mapped to 16 column-major entries of a proper rigid 4x4 transform. Translations occupy entries 12–14. Combined component frame coverage is limited to 262,144 frames.
- `operationTracks`: exactly every operation ID, each mapped to exactly its participating source placement keys. Each path contains up to eight `RotationTrackStep` records: numeric `axis` and `pivot` triples, finite signed `angle` in radians with absolute value at most 100, and `space: "world" | "local"`. Axes are normalized when loading; degenerate axes are rejected. Combined paths are limited to 524,288 rotation steps.

The optional `shared-rotation-tracks` capability adds two document-local libraries without changing methods, ownership, buffer layouts, or schema version 1. `rotationTracks` maps names to arrays of up to eight rotation steps. A placement path may be a string naming one of these arrays. `operationTrackTables` maps names to complete operation/placement tables; a component's `operationTracks` may be a string naming one of these tables. Names resolve only inside the same realization. Unknown names, missing capability declarations, malformed steps, and incorrect endpoints are rejected. At most 8,192 named paths and 4,096 named tables are accepted. The interpreter checks every referenced path against its exact abstract transport, including different axes for different participants in a paired turn. Components in one domain with identical frames, the same named table, and no declared mesh symmetries share one verified pose table; frame/step limits count that table once. Assets, colors, picking identities, and one output transform per visual part remain independent. Older runtimes must be updated before loading documents requiring this capability; `kernelInfoJSON().geometryCapabilities` advertises support.

Melty A–D are offline-authored packages at `packages/melty-{a,b,c,d}/`, with 2,320 / 1,474 / 2,721 / 4,303 placements and 72 / 48 / 76 / 108 single-cap requests. Each exposes six independent control families. Local middle alignment permits stops even when the opposite cap obstructs the intersecting full cut; old paired requests are replaced by two single requests with separate history entries. Whole-piece cut blockers, center phases, local alignment and exact solid compatibility compile to existing membership/count predicates. The larger catalogs preserve old placement IDs as prefixes; manifests distinguish witnessed placements from conservative candidates. A uses port bindings; B–D use articulated bindings sharing a local move only when its complete transport and signed driving path agree. Shared generator tracks and transport selectors serve the Euclidean, spherical, and port-diagram realizations. Each `melty-?-spherical.json` uses `articulated-spherical`, radius 2.05, smooth radial normals, the cube's exact axes and pose tables, and the unit-sphere sections at normalized cut depth 1/3. The reference app defaults to Cube + sphere; all three documents are included in the SDK. This geometry addition preserves the existing abstract rules and definition pins. The earlier middle-alignment rule revision changed definition pins; old saves require their matching older definitions. Public APIs and buffer versions are unchanged by this rule revision.

The [110-piece face-turning icosahedron](icosahedron-model.md) supplies `articulated-euclidean` and `articulated-spherical` documents. Its twenty axes, nine center stops, 1,080 shared poses, 280 requests, and spherical overlap constraints use existing rule and geometry APIs. The Euclidean model cuts a regular icosahedron with conical surfaces matching the spherical disks. Port material IDs use CSS hex colors; custom renderers can interpret these opaque labels directly. Both documents are included in the SDK inventory. The 152-piece preset supplies the same two realization kinds, using planar Euclidean cells and spherical legality. Its four shared domains have 2,880 placements and 280 relative requests; it has no absolute-phase control catalog. Consumers use ordinary session planning/execution for these relative requests. The reference app schedules these queries outside the kernel. Both 152-piece documents are also included in the SDK inventory.

Vertex, axis, pivot, and frame coordinates must be finite and have absolute value at most `Float32.max / 128` (approximately `2.66e36`). This reserves buffer headroom for eight composed pivot rotations and rejects finite doubles that would overflow renderer buffers.

At progress `t`, the interpreter uses `s = t*t*(3-2*t)`. Starting with the source frame, each rotation is applied using angle `s*angle`, in authored order: multiply on the left for world space and on the right for local space. A rotation about pivot `c` maps a point to `c + R*(p-c)`. Thus each primitive rotation is simple in its declared frame while world motion can compose carrier and cog rotation. No matrix interpolation or shortest-path replacement changes a mechanical path. A diagram additionally interpolates translation between chart endpoints; its rotation tracks remain explicit and schematic.

Registration verifies participant endpoints against target frames, up to declared component symmetries. Failures use `realization.articulated`; digest mismatches retain `realization.digest_mismatch`. A malformed recorded transition retains the existing witness diagnostics and does not replace a prepared animation. `prepareAnimationJSON` returns `durationHint` based on the largest participating component's summed angular travel; controllers retain timing ownership. The interpreter never commits logical state.

`articulated-spherical` additionally requires a finite `radius` in `(0,100]`. Asset coordinates already use this display radius; the interpreter applies no extra scale. Every vertex must lie within `radius + 1e-6`, every placement frame must have exactly zero translation, and every world/local rotation pivot must be the origin. These restrictions preserve each vertex's radial distance throughout motion, including inner shells and an internal ball. Invalid values produce `realization.articulated`. The scene reports `ambientSpace: "S2"`, `sphereRadius`, authored `fidelity`, and `catalogTransportVerified: true`. This kind verifies rigid catalog transports; it does not assert `surfaceTransportVerified`, continuous coverage, or gear contact certification. Geometry ownership, borrowing, constructor signatures, errors, and triangle/frame buffer layouts otherwise remain unchanged.

Articulated assets may supply `radialNormalTriangles`: a list of distinct, zero-based triangle indices into that asset's `triangles`. For each marked triangle, C++ emits normalized vertex-position normals about the local origin, directed outward or inward according to the triangle winding. Vertices must be away from the origin and the face must have an unambiguous radial orientation. Unmarked triangles retain flat face normals, allowing sharp cut walls beside smooth shells. Masks are supported in every articulated realization kind, including a spherical internal ball in an otherwise Euclidean model; they specify local shading independently of the ambient space. Malformed, duplicate, or out-of-range indices produce `realization.articulated`. Declared component symmetries must preserve boundaries between radial and flat shading as well as the geometric boundaries. Positions, indices, frame sampling, and buffer sizes are unchanged. This additive extension of the existing optional field retains document-schema and buffer version 1; custom renderers consume the existing normal buffer without additional shader logic. Runtimes predating Euclidean radial shading must be updated to load Euclidean documents with nonempty masks.

All visual components bind to their persistent abstract `pieceId`; ports retain `portId`. Multiple components do not create duplicate core occupants. The scene and one 16-Float32 transform per visual part use the existing public layout, ownership, borrowing, and disposal rules. Three.js and the independent Canvas/native consumers require no gear-specific kinematics.

The normal Gear Cube's `gear/gear-normal-euclidean.json` contains the supplied model's 38 exterior bodies and one fixed internal ball, using split port patches and a body `pieceIds` filter. It stores meshes in the same JSON format as the other articulated realizations; the original binary model is not needed. These additions change no public methods, semantic digests, buffer layouts, or version-1 documents already supported by the kernel. Rendering needs only triangle meshes and rigid transforms. The independent package includes one cubic model, one spherical model, and one phase diagram per Gear preset under `examples/canvas/data/`. Custom masks reuse the Extreme cubic template and the shared spherical/diagram templates with the custom semantic pin. Native and WASM clients select them through the existing geometry constructor. Six-tooth tracks preserve the full signed 300° travel per phase step. See [gear authoring](gear-model.md) for storage, shared identity, and contact limitations.

The Extreme and all-plain presets use `*-supplied-euclidean.json` realizations with the shared normal meshes and the Extreme model's rigid plain edge. Plain edges instantiate one body and two port patches, with identical frames/tracks and no local spin. `gear-extreme/plain-edge-meshes.json` preserves the three source assets. Custom controllers can pair `gear-extreme/gear-extreme-supplied-euclidean.json` with a compiled custom gear definition by replacing its `compatibleDefinitionDigest` and realization ID, as the catalog domains and transports remain shared. No API, buffer, or schema version changes are needed.

The supplied spherical model is packaged as `gear/gear-spherical.json`, `gear-extreme/gear-extreme-spherical.json`, and `gear-plain/gear-plain-spherical.json` under `examples/canvas/data/`. Its shared source assets are `gear/spherical-meshes.json`. Custom clients may similarly pin the spherical template to a custom compiled gear definition. The model's texture-mask sticker borders are baked into body/port triangles, so rendering requires no GLB loader, texture support, or additional buffer fields. These realizations retain six-tooth 300° cog tracks, plain-edge rigid tracks, and shared abstract identities. The independent native and WASM consumer checks cover all three spherical presets and a custom mask.

## Prebuilt puzzle data and packaging

The SDK packager copies runtime artifacts from the same inventory used for puzzle generation and freshness validation. It rejects stale or modified puzzle artifacts before copying. `npm run build:puzzles` is the explicit offline authoring step; normal web/kernel builds consume the checked-in packages and validate them against the current WASM engine. The repository manifest is build provenance, not a public kernel format or API version, and consumers do not need it to load a supported puzzle package.

### S³ cell turns and stereographic drawings

The `s3-cell-turning` realization requires `triangle-meshes`, `so4-isometries`,
`stereographic-projection`, and `dynamic-drawing-meshes`. API 7 gains the additive
`GeometryDefinition.project_with_options(frame, projection, options_json)` method
(`projectWithOptions` in WASM). Existing `project` remains available. Geometry,
pose, and drawing buffer versions remain 1; discover capabilities before loading.

`space.id` is `S3`: intrinsic dimension 3, Euclidean embedding dimension 4,
unit radius, positive curvature, and `homogeneous: false`. Float64 column-major
4×4 pose matrices are genuine SO(4) matrices, not affine R³ transforms. Authored
assets contain unit four-coordinate vertices and unit tangent outward normals.
The default `cellShape: "cube"` binds colored prototype meshes to signed-permutation placement frames.
`cutDepth` lies in (0,0.5); `pieceInset` lies in [0,1). An inset vertex is
`normalize((1-pieceInset)*p + pieceInset*c)`, where p is on a cut surface and c
is the normalized sum of the piece’s active positive coordinate axes. Normals
follow this map. Construction verifies cut membership, outward normals, color
sectors, and invariance under unmarked endpoint stabilizers.
Each operation track contains its signed coordinate `cell`, orthonormal
`planeU` and `planeV`, and directed `angle` in radians. Sampling rotates this
plane and fixes its orthogonal complement. Exact abstract transports include
all 27 participants, even the unmarked center and other self-loops.

The additional `s3-octahedral-cells` capability and `cellShape: "octahedron"` select the ordinary 24-cell. Its cut
normals are the eight signed coordinate vectors and the sixteen vectors
`(±1,±1,±1,±1)/2`. Supported depths satisfy `1/√3 < cutDepth < 1/√2`;
the 672-piece model uses `2/3`. The additional required capability
`s3-octahedral-tangent-cuts` permits the endpoint `cutDepth: 0.7071067811865476`
(45° radius), used by `cell24-192`: 192 movable pieces plus 24 fixed unmarked
centers. This capability is valid only at that endpoint and must accompany
`s3-octahedral-cells`; older runtimes reject the unsupported capability.
It adds no fields, buffers, or native symbols; API version 7, kernel version
0.7.0, and buffer versions remain unchanged. Each domain supplies `cutMembership` (24 booleans,
coordinate normals first in X/Y/Z/W order, positive before negative, followed
by half-coordinate sign tuples in lexicographic order with positive first),
`colorNormals` (ordered visible sector normals), and unit `pieceCenter`, in
place of `activeCoordinates`. Membership and color counts differ. Frames are
proper rotations preserving all 24 normals, including half-integral entries.
Track `cell` is a layout cell ID: the eight coordinate names above or
`H` followed by four `p`/`m` signs. Every ordinary turn contains 105 pieces in
the 672-piece model or 21 in the 45° variant.
The compiler accepts up to 200,000 predicate nodes; the shipped package remains
inside the 16 MiB execution/source bounds. No jumbling requests are authored.

Use projection `"stereographic"`; `"embedding"` is unsupported for this space.
Options are a JSON object with optional fields:

- `version: 1` (default).
- `focusCell`: `Xp`, `Xm`, `Yp`, `Ym`, `Zp`, `Zm`, `Wp`, or `Wm`. The chosen
  cell is moved to −W by a proper SO(4) view isometry. The 24-cell also accepts
  its sixteen `Hpppp`–`Hmmmm` layout IDs. Default `Wm` is identity.
- `viewIsometry`: an explicit 16-element column-major SO(4) matrix, mutually
  exclusive with `focusCell`.
- `scale`: positive display scale in [0.01,20], default 1.
- `clipRadius`: radius in unscaled stereographic coordinates in [0.5,32],
  default 8 for the 8-cell and 16 for the 24-cell. The emitted radius is bounded by `scale * clipRadius`.
- `guide: {group, axis}`: IDs from the realization's cell-axis layout.

After the view isometry, projection is `scale * (x,y,z)/(1-w)`. The kernel
clips on S³ before dividing; it never clamps the denominator. Conservative
triangle bounds and bounded subdivision detect pole crossings inside triangles.
The final clipping boundary is a tessellated approximation. Normals follow the
projection differential. Drawings contain deformed R³ Float32 positions/normals,
local Uint32 triangle indices, and identity drawing transforms. Asset IDs and
visual-part order are stable, but offsets, counts, and topology may change each
sample. Always read **the drawing's** scene and buffers together. Definition
buffers describe only the initial default drawing. Each drawing owns its mesh
storage, survives subsequent projections and deletion of the definition, and
exposes borrowed native references/WASM views under the existing lifetime rules.

Scene metadata adds `dynamicMeshes: true`, `ambientSpace: "S3"`, and
`projection: {version, kind, viewIsometry, scale, clipRadius, clippedTriangles,
outerPieceIds, pieceDepths}`. `outerPieceIds` identifies pieces containing the pole before
display inset. Renderers may make these transparent or hide them for picking.
`pieceDepths` maps each piece ID to its normalized intrinsic center’s w coordinate
after the view isometry. Renderers can fade the outer hemisphere smoothly to
expose the inner layers. An optional `axisGuide` is an array of R³ polylines; `guidePieces` lists the cap's
persistent piece IDs. These are presentation data and never alter puzzle state.
Projection validation failures use `geometry.projection`; malformed JSON or
incorrect JSON field types may raise JSON parse/type errors. Invalid S³ meshes,
frames, layout, capabilities or transport agreement produce `realization.s3`.
Existing frame-ownership and exact-witness diagnostics still apply.

`controlLayout: {version: 2, kind: "cell-axes", groups}` contains eight cubic or 24 octahedral groups
with `id`, `label`, four-coordinate `center`, and three `categories`. Each
category has `kind: "face" | "edge" | "vertex"` and `axes`; each axis has `id`,
`label`, `direction`, and `turns: [{operation, angleDegrees}]`. These are relative
turns submitted through revision-checked session requests. There is no absolute
phase driver or accumulated scalar angle for noncommuting axes. Version-1 driver
layouts are unchanged.

The TypeScript `GeometryInstance` helper retains the current intrinsic frame.
For dynamic meshes its `positions`, `normals`, `indices`, `matrices`, and `scene`
are owned drawing snapshots, updated by default `setState`, `prepare`,
`evaluate`, or `setProjection(options)`. `projectionOptions` returns a copy.
Rejected options leave the previous drawing and animation intact. Matrix-only
`destination(record)` rejects dynamic meshes; use independent `animate` /
`projectWithOptions` handles to render a deformed destination. Dispose the helper
to release the retained frame. The Canvas example and installed native example
consume this path without Three.js or the reference application.

The additive `intrinsic-meshes` capability and `intrinsicMeshBufferVersion: 1`
in `kernelInfoJSON()` expose `GeometryDefinition.intrinsic_mesh()` /
`intrinsicMesh()`. This returns a `GeometryIntrinsicMesh` snapshot with
`descriptor_json()` / `descriptorJSON()` and `positions()`, `normals()`,
`indices()`. It shares immutable storage and survives deletion of its definition,
session, frames, and other snapshots. Native references and WASM typed views are
borrowed from that handle; delete WASM handles explicitly, and copy views before
deleting them or calling allocating WASM code. Treat borrowed buffers as read-only.
`copyIntrinsicMeshes(definition)`
returns owned JS arrays. Unsupported realizations throw `geometry.intrinsic_mesh`.
Existing APIs, pose/drawing buffers, realization schemas and semantic pins are
unchanged; discover this optional capability before calling the new method.

The intrinsic descriptor contains `version: 1`, `space`, `coordinateSize: 4`,
`scalar: "float32"`, `normalModel: "unit-sphere-tangent"`, `meshAssets`, and
`visualParts`. Positions and normals pack four Float32 coordinates per vertex;
normals are unit outward vectors tangent to S³. Asset fields `positionOffset`,
`normalOffset`, and `indexOffset` count scalars in their respective packed arrays;
`vertexCount` counts vertices and `indexCount` counts Uint32 indices. Indices are
local to the asset. Triangles wind outward after an orientation-preserving
stereographic projection. Prototypes are shared; intrinsic asset IDs/offsets
differ from drawing asset IDs/offsets. Intrinsic visual parts retain the drawing's
order, persistent IDs and material bindings, but reference these prototype IDs.
Each supplies either the cubic `activeCoordinates` count, or the 24-cell
`cutHalfspaces: [{normal: [x,y,z,w], minimum}]`. A point lies in the unshrunk
piece exactly when `dot(normal,p) >= minimum` for all halfspaces; positive
minimums identify the complete turning-ball membership. This permits pole
containment and guide selection without inferring cuts from colors. Both supply
`boundingCap: {center: [x,y,z,w], minDot}`. The unit center is the piece's local
center; all normalized triangle interiors lie in its positive spherical cap
`dot(center,p) >= minDot > 0`. Bounds use double precision; allow a numerical
margin when applying them to Float32 GPU buffers. Positive `cutHalfspaces`
provide additional convex-cap bounds: the minimum of their W bounds and the
piece's bounding-cap bound safely rejects projection-pole crossings even during
half-integral 24-cell rotations. Negative-minimum halfspaces are not convex
spherical caps and must not be used for this normalized-triangle bound.

A custom renderer uploads these prototypes once and applies
`q = viewIsometry * partIsometry * intrinsicPosition`. Its projected position is
`scale*q.xyz/(1-q.w)`. For `n = viewIsometry * partIsometry * intrinsicNormal`,
the projected normal is `normalize((1-q.w)*n.xyz + q.xyz*n.w)`. The view and pose
are column-major SO(4) matrices. Do not apply an affine translation or mesh
inset again. Renderers must avoid dividing across the pole: if the cap center
has transformed W-coordinate `d` and `c=minDot`, its maximum W is 1 for `d>=c`,
otherwise `d*c + sqrt((1-d*d)*(1-c*c))`. A cap below
`(clipRadius²-1)/(clipRadius²+1)` is safe for GPU projection. Otherwise use the
portable clipped drawing. A cached clipped drawing remains valid up to an R³
rotation when the combined SO(4) matrix's last row is unchanged; if it changes,
refresh the drawing. This preserves clipping topology and projected normals.

The helper's owned `isometries: Float64Array` always follows its retained frame.
`prepare(record, false)`, `setState(stateText, false)`, and
`evaluatePose(progress)` update only this frame and its poses. The previous
drawing buffers and `scene` remain available, with
`drawingIsCurrent: false`; `refreshProjection()` explicitly brings them current
without cancelling the animation. Deferred `setState` installs the exact validated
state and cancels the previous animation; an invalid state preserves both the
current frame and animation. Drawing arrays and scene remain owned snapshots
of the last projection, rather than describing the new poses. `drawingRevision`
increments after successful projection, allowing clients to avoid repeated
copies. Default `prepare`, `evaluate`, `setState(stateText)`, and `setProjection`
still project immediately. The app
uses shaders for safe caps, cached clipped meshes at the pole, and fresh CPU
projection if a changed cap needs clipping. CPU raycasting refreshes the drawing
on demand; selection stays disabled during animation. Three.js materials,
shaders, camera state and scheduling remain outside the kernel.

The reference renderer installs final S³ states with deferred projection and
refreshes the CPU drawing only for picking, changed projection options, or a
clipping fallback. Transparent S³ surfaces use cached BackSide/FrontSide
materials on two full-range groups of the same sortable mesh, preserving the
original back/front order. Both materials share that part's shader uniforms;
selection colors, emissive intensity, opacity and depth settings stay in sync.
Their side configurations stay fixed across frames. Opaque surfaces retain a
single material and draw. These renderer policies do not change native/WASM
contracts or buffer layouts. The optional TypeScript helper argument is backward compatible;
API and buffer versions are unchanged.

The app shows pieces belonging to the focused cell or any face neighbor, using
their complete positive cut memberships. At settled poses, more distant pieces
are hidden; during turns, the membership weight interpolates between alignment
rings. Vertex opacity is `membership * (1-smoothstep(startRadius,endRadius,r))`,
where `r = sqrt((1+w)/(1-w))` is projected radius in units of projection scale.
The slider adjusts the start radius, constrained to keep the focused cutting
ball opaque. A second slider sets the end radius, initially enclosing all
neighboring cutting balls. Moving it below the start lowers the start too;
there is always at least 0.01 unit between the radii. Fragments beyond the end
are discarded, including on triangles crossing that radius. The pole-containing
piece remains hidden, using the same containment rule as `outerPieceIds`.
Cached CPU meshes recover radius directly from their
positions, so changing the fade needs no new drawing or buffer copies. The
two radii are saved in the app's camera presentation as `fadeStartRadius` and
`fadeEndRadius`; neither is a kernel projection option. These optional fields
remain compatible with older saves. The app's S³ camera can zoom inward to 0.05 units
with a 0.005 near plane. Fully transparent surfaces are hidden,
fully opaque ones write depth, and fading surfaces use alpha blending. Picking
passes through fading surfaces to opaque pieces. This is renderer presentation
policy, independent of the kernel's geometric buffers and puzzle rules.
