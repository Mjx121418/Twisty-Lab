# Portable kernel API

The C++ abstract core, compiler, session, and geometric interpreter form the reusable kernel. Native C++ applications and JavaScript/TypeScript applications using WebAssembly consume the same implementation. Renderers and controllers are clients: Three.js, React, Vite, Canvas, camera controls, input gestures, and animation clocks stay outside the kernel.

The current kernel version is `0.4.0`, public API version `4`, and frame-buffer version `1`. The runtime accepts relational package/definition format 1, the Gear wrapper, and session schema 2. API 4 normalizes the core, compiler, and authoring API names after removal of the legacy runtime in API 3. Semantic digests, saved sessions, and geometry buffer layouts are unchanged. `twisty::kernel_info()` and WASM `kernelInfoJSON()` report supported `documentKinds`, `sessionSchemaVersions`, and rule modules independently. Native consumers compile against the installed C++ headers and compatible toolchain; a cross-compiler binary ABI is not promised.

The [puzzle language API](core-language.md), advertised as `languageAPIVersion = 1`, exposes the JSON language and domain-free executor through `Twisty::core`, `Twisty::language`, WASM `Puzzle`, and the TypeScript `Puzzle` adapter. Completion selection and bounded discovery are separate from permission checking. The cube/bandage authoring library is exposed as C++ `language::cube_module()`, WASM `cubeModuleJSON()`, and TypeScript `createCubeSource()`; `authoringLibraries` lists the supported models. `Twisty::symmetry` provides the shared standard-C++ finite-group utilities. Every shipped preset, including Bagua and custom Gear masks, uses the same core through an explicit package adapter. The pure program preview remains separate from the persisted definition format. Current session schemas refer to the standalone state schema; the retired finite-definition schema is not shipped.

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

The native package exports `Twisty::core`, `Twisty::language`, `Twisty::execution`, `Twisty::symmetry`, `Twisty::session`, `Twisty::geometry`, `Twisty::catalog`, and aggregate `Twisty::kernel` targets. A consumer can use:

```cmake
find_package(TwistyKernel 0.4 CONFIG REQUIRED)
target_link_libraries(my_frontend PRIVATE Twisty::kernel)
```

Configure that consumer with `-DCMAKE_PREFIX_PATH=/your/kernel/install`. The package includes public headers and its JSON dependency. A headless consumer may link `Twisty::session` without geometry. Both bindings are checked on the current Linux ARM64 container; native packages should be built for the destination platform and compiler.

```cpp
#include <twisty/kernel.hpp>

twisty::Session session(definition_json);
twisty::Geometry view(session.execution(), realization_json);
auto result = session.execute("U", session.revision());
for (const auto &transition : result.at("transitions")) {
  view.prepare_animation_json(transition.dump());
  view.sample(0.5);
  // Copy/use view.transforms() and the reusable scene assets in your renderer.
}
```

JavaScript clients import the package's public entry point:

```js
import { createKernel, KernelSession, copyFloatView } from '@twisty/kernel';

const kernel = await createKernel({ wasmURL: '/assets/twisty.wasm' });
const session = new KernelSession(kernel, definitionText);
const view = kernel.createGeometry(session.native, realizationText);
if (!view) throw new Error('Cannot create interpreter');

const source = session.snapshot();
const request = { operation: 'U', parameters: {} };
const preview = session.plan(request); // Pure; does not change session/history.
if (preview.status === 'Legal') {
  const result = session.execute(request, source.revision);
  for (const record of result.transitionRecords ?? []) {
    const prepared = JSON.parse(view.prepareAnimationJSON(record));
    if (prepared.status !== 'Prepared') throw new Error('Unsupported visual transition');
    view.sample(0.5);
    const matrices = copyFloatView(view.transforms());
    // Draw using your renderer, then continue sampling until progress reaches 1.
  }
}
view.delete();
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

Execution source packages and persisted definitions are limited to 16 MiB; language source text remains limited to 8 MiB. Definition CLI output is compact JSON so large definitions can be read back within that limit. Reference app and CLI session imports retain their separate 4 MiB limit.

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

Create a C++ `Geometry` from the immutable definition pointer and a compatible realization document, or use WASM `createGeometry(session, realizationText)`. The geometry owns its definition reference and can outlive the session. Each realization independently validates its geometric transports and ports. The existing string-based geometry constructor remains available for standalone documents.

| Task | C++ | WASM |
| --- | --- | --- |
| Read stable scene/part bindings | `scene_json()` | `sceneJSON()` |
| Read reusable assets | `positions()`, `normals()`, `indices()` | same names |
| Realize a resting state | `set_state_json(stateText)` | `setStateJSON(stateText)` |
| Prepare a recorded transition | `prepare_animation_json(record)` | `prepareAnimationJSON(record)` |
| Sample motion | `sample(progress)` | `sample(progress)` |
| Read current object matrices | `transforms()` | `transforms()` |
| Resolve a picked visual part | `bind_hit_json(visualPartId)` | `bindHitJSON(visualPartId)` |

`setStateJSON` changes only that interpreter's displayed state. It cannot modify the session. A preparation result is `Prepared` with a duration hint or `UnsupportedTransition` with diagnostics. A visual limitation does not change abstract legality. The controller supplies normalized progress and chooses an animation clock; the kernel schedules no frames.

The public `SceneDescriptor`, `MeshAsset`, `VisualPart`, and `HitBinding` TypeScript types describe the wire data. Check `requiredCapabilities` against your renderer before constructing drawing resources. Current capabilities are `triangle-meshes`, `rigid-transforms`, and `quotient-surface`. The `diagram` flag identifies flat port diagrams. Material bindings are symbolic labels; the renderer chooses colors, lighting, fonts, and selection/blocking styles.

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

`kernelInfoJSON()` also advertises the `cube-spherical` realization. Its optional scene metadata identifies `ambientSpace: "S2"`, the six `diskCenters`, `sphereRadius`, `diskAngleDegrees`, `fidelity`, and `surfaceTransportVerified`. Mesh assets may declare `rotationSymmetryOrder`; featureless spherical centers use order four. These additive descriptors use the same buffer format and rendering capabilities as other realizations. Custom clients pass a compatible `cube-spherical.json` into the existing geometry constructor; the packaged example data includes both reference cube realizations. See [the spherical construction](spherical-cube.md) for its geometric verification and surface-model scope.

`polyhedral-spherical` intersects catalog prototypes with the unit sphere and uses the existing catalog frames and rotation tracks. The Helicopter package provides twelve 45° disks, seven shared assets, 44 existing pieces, and 48 ports. Local mesh vertices lie on unit shells; the frame includes the uniform display scale, equal to `sphereRadius`. It retains the abstract model's volume, phase, catalog, and exclusion guards; a spherical outline can therefore show tangency for a request blocked by a volume-derived rule. See [the spherical Helicopter construction](spherical-helicopter.md). The copied example data includes `helicopter/helicopter-spherical.json`.

Polyhedral model `ports` map each local port ID to 3–32 distinct model vertex indices. The indices may be unordered; they must describe a coplanar, strictly convex polygon on an exterior supporting plane. The interpreter orders its boundary, triangulates one shared mesh asset, and uses the arithmetic mean of its vertices as the port anchor. Malformed polygons produce `realization.catalog` diagnostics. The experimental [Bagua model](bagua-model.md) demonstrates 146 pieces and 198 polygonal ports using the existing session/planning APIs and Euclidean, diagram and spherical documents.

`PolyhedralModel.surfaceVertices` is an optional array of 4–32 finite numeric triples, consumed by `polyhedral-spherical` only. It specifies the convex hull intersected with the **unit sphere**, in the same local frame as the original model. Omission falls back to `vertices`, preserving existing packages. Original `vertices` and `ports` continue to define port normals, identity and catalog checks; port indices never address `surfaceVertices`. Every declared symmetry must preserve both vertex sets and each labeled port. Malformed surface arrays and incompatible symmetries produce `realization.catalog`; degenerate sections, incompatible disk angles and failed continuous participant/stationary membership produce `realization.spherical`. Every active surface cut must have absolute unit-normal offset `cos(diskAngleDegrees)`.

The [spherical Bagua package](spherical-bagua.md) uses this extension to supply six approximately 66.11° disks and twenty shared assets while retaining all 146 pieces and 198 ports. Its copied document is `examples/canvas/data/bagua/bagua-spherical.json`. These additive schema/type fields originally shipped in API 1 and retain **buffer version 1**, with unchanged ownership, constructor signatures, capability names, indexed triangles and frame layouts. No Bagua-specific renderer API is required.

Buffer version 1:

- Positions and normals are packed `Float32` triples. `positionOffset` is an offset in scalar elements, and `vertexCount` counts vertices. Position and normal slices have the same offsets and lengths.
- Indices are packed `Uint32`. `indexOffset` and `indexCount` count index elements. Indices are local to the corresponding asset's vertex slice.
- Matrices are column-major `Float32` 4×4 values. Matrix `i` occupies elements `16*i` through `16*i+15` and corresponds to `visualParts[i]`.
- Stable scene, piece, port, visual-part, and asset IDs connect logical and visual records. Dense array order is local to that scene, not a persistent puzzle identity.

Native buffer references and WASM typed views are borrowed. Copy exported WASM views immediately before another kernel call can invalidate their backing memory. Use `copyFloatView()` / `copyIndexView()` for assets and reusable destination arrays for sampled transforms. Do not serialize frames as JSON or keep heap views as application-owned storage. Explicitly call `delete()` on geometry handles and `dispose()` on adapter sessions; dispose front-end resources separately.

The renderer performs hit testing. Send the hit visual-part ID back to the interpreter to obtain a piece, optional port, and candidate requests. Candidates still need core planning. In the current interaction contract, bind puzzle picks at resting frames and disable state-dependent picking during animation. The controller owns displayed-frame tags, selections, queues, and any synchronized views; camera movement is independent of logical state.

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

`kernelInfoJSON().realizationKinds` advertises `articulated-euclidean`, `articulated-port-diagram`, and `articulated-spherical`. These additive realization capabilities originally shipped in API 1; their document schema and frame-buffer version remain 1. Clients discover supported kinds before loading documents. Existing kinds and semantic digests are unchanged. The compiler accepts `GearCubeSource` (`kind: "gear-cube-prototypes"`, `schemaVersion: 1`, `semanticsVersion`, `puzzleId`, and a distinct `gearedEdges` subset of the twelve conventional edge IDs). It emits the existing geometry-free placement-relations definition. See [the experimental gear specification](gear-model.md) for its coupling policy.

An articulated document is pinned by `compatibleDefinitionDigest` and has:

- `assets`: a map of IDs to numeric `vertices` triples and integer `triangles` triples. Indexed meshes may be non-convex. C++ expands triangles into the same borrowed position/normal/index buffers as existing realizations; outward winding is authored, and normals default to flat per-triangle values. Each asset has 3–65,536 vertices and 1–262,144 triangles; at most 256 assets, 262,144 combined vertices, and 524,288 combined triangles are accepted.
- `componentsByDomain`: exactly every placement domain, each with 1–32 component records. Each component supplies `id`, `meshAssetId`, `role` (`body` or `port`), `placementFrames`, and `operationTracks`. Port components additionally supply `portId`; every local port is bound at least once. Several patches may share a port ID and have different frames/tracks; their unique component IDs produce distinct visual-part IDs, while color labels and hit bindings remain shared. Optional body `materialBindingId` selects appearance. Optional body-only `pieceIds` filters instantiation to a nonempty list of distinct existing pieces in that domain; port components cannot use this filter, so port coverage remains complete. Frames/tracks still cover the whole domain. Optional `symmetries` are at most 32 proper 4x4 label-preserving mesh symmetries, verified against vertices and oriented mesh boundaries/normals. Declaring them requires at most 4,096 asset vertices and 16,384 boundary edges.
- `placementFrames`: exactly every placement key, each mapped to 16 column-major entries of a proper rigid 4x4 transform. Translations occupy entries 12–14. Combined component frame coverage is limited to 262,144 frames.
- `operationTracks`: exactly every operation ID, each mapped to exactly its participating source placement keys. Each path contains up to eight `RotationTrackStep` records: numeric `axis` and `pivot` triples, finite signed `angle` in radians with absolute value at most 100, and `space: "world" | "local"`. Axes are normalized when loading; degenerate axes are rejected. Combined paths are limited to 524,288 rotation steps.

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
