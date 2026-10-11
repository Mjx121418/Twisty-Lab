# Portable kernel API

The C++ abstract core, compiler, session, and geometric interpreter form the reusable kernel. Native C++ applications and JavaScript/TypeScript applications using WebAssembly consume the same implementation. Renderers and controllers are clients: Three.js, React, Vite, Canvas, camera controls, input gestures, and animation clocks stay outside the kernel.

The current kernel version is `0.1.0`, public API version `1`, document schema version `1`, and frame-buffer version `1`. `twisty::kernel_info()` and WASM `kernelInfoJSON()` report these independently of each puzzle's semantic digest. This is the first public development API; future incompatible contract changes require a new API or buffer version. Native consumers compile against the installed C++ headers and compatible toolchain; a cross-compiler binary ABI is not promised. Supported bindings currently cover C++ and JavaScript/TypeScript. Additional language bindings can use the same contracts later.

## Build and consume

```sh
npm run build:kernel
cmake --install build/native --prefix /your/kernel/install
```

This builds the C++ libraries and WASM runtime, and assembles the independent ES-module package in `build/kernel/`. It does not build or start the React/Three.js app. Nothing is published to a registry. Copy the entire `build/kernel/` directory to another project or install that local directory as an npm package. Its runtime has no external JavaScript dependencies. TypeScript declarations include both the convenient adapter and generated low-level bindings.

The native package exports `Twisty::core`, `Twisty::compiler`, `Twisty::session`, `Twisty::geometry`, and aggregate `Twisty::kernel` targets. A consumer can use:

```cmake
find_package(TwistyKernel 0.1 CONFIG REQUIRED)
target_link_libraries(my_frontend PRIVATE Twisty::kernel)
```

Configure that consumer with `-DCMAKE_PREFIX_PATH=/your/kernel/install`. The package includes public headers and its JSON dependency. A headless consumer may link `Twisty::session` without geometry. Both bindings are checked on the current Linux ARM64 container; native packages should be built for the destination platform and compiler.

```cpp
#include <twisty/kernel.hpp>

twisty::Session session(definition_json);
twisty::Geometry view(session.definition(), realization_json);
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

| Task | C++ | WASM / TypeScript |
| --- | --- | --- |
| Discover versions and capabilities | `kernel_info()` | `kernelInfoJSON()` |
| Compile/validate a definition | `compile_source()`, `load_definition()` | `compileJSON(source)`; `new Session(source)` |
| Read definition and exact state | `definition()`, `state()`, `definition_json()`, `state_json()` | `definitionJSON()`, `stateJSON()`; adapter `definitionText`, `stateText()` |
| Inspect revision, goal, legal requests, history | `snapshot()`, `revision()` | `snapshotJSON()`, `revision()`; adapter `snapshot()` |
| Validate an assignment | `decode_state()`, `validate_state()` | `validateStateJSON(stateText)`; adapter `validateState(stateText)` |
| Plan without committing | `plan(definition, state, operation)` | `planJSON(requestText, stateText)`; adapter `plan(request, stateText?)` |
| Commit a primitive | `execute(operation, expectedRevision)` | `executeJSON(requestText, expectedRevision)`; adapter `execute(request, expectedRevision?)` |
| Algorithms and seeded legal walks | `run()`, `scramble()` | `runJSON()`, `scrambleJSON()`; adapter `run()`, `scramble()` |
| Linear history | `undo()`, `redo()` | `undoJSON()`, `redoJSON()`; adapter `undo()`, `redo()` |
| Persistence and checked replay | `save()`, `load()` | `saveJSON()`, `saveWithPresentationJSON()`, `loadJSON()`; adapter `save(presentation?)`, `load(document)` |

Requests contain a directed operation ID and `parameters: {}` for the currently registered primitives. Planning returns `Legal` with an immutable transition and a lossless `transitionRecord`, or `Blocked` with structured evidence. JSON wrappers return `Invalid` and diagnostics for invalid input; constructors and native typed APIs signal errors with exceptions. Assignment validation returns `Valid` or `Invalid`; validity does not establish reachability.

`finite-placement-relations@2` extends the version 1 rule module with optional `placementCountGuards`. Each guard contains a unique `id`, a `placementDomainId`, a nonempty set of distinct `placements`, and inclusive integer `min`/`max` bounds (`0 <= min <= max <= 4096`). It counts persistent pieces in that domain whose **current** placements belong to the named set. Every guard must pass, in addition to piece, mechanism, occupancy, and placement-relation constraints. Inverse guards have matching IDs, domains and bounds, and count exactly the transported placement set. Counts are derived from the supplied state; they add no state fields or geometric data. Pure planning, legal-request enumeration, replay, and commits all evaluate the same guards.

A failed count returns `Blocked`, `reasonCode: "placement.count"`, the guard ID as `constraintId`, matching persistent IDs in `implicatedPieces` (possibly empty), and `evidence: {count, min, max, placementDomainId}`. Malformed guards report `guard.count`; invalid inverse guards report `inverse.count_guard`. Version 1 modules reject count-guard fields. Older kernels reject the new rule-module ID, so they cannot silently omit its conditions. Discover support through `kernel_info().ruleModules` / `kernelInfoJSON()`. This adds a rule capability and TypeScript `PlacementCountGuard` declaration; API, buffer and document-schema versions remain 1.

Only session commands commit state. Successful commands return `Committed` with ordered transition records and a snapshot. A stale expected revision returns `StaleRevision`; blocked or stale commands preserve state and history. Algorithms use `interactive` (keep the legal prefix) or `transactional` (commit all or none) policies. Adapter command methods default to the current revision for synchronous controllers. Controllers that plan asynchronously should capture a revision and pass it explicitly.

Call each session and geometric instance sequentially on its owning thread. The controller owns request queues and scheduling; the kernel does not synchronize concurrent calls to one mutable handle. Separate worker-owned instances can exchange immutable serialized records through host-provided orchestration.

The pure plan API accepts a separate source-state document for previews and analysis. Its state digests identify the source; that preview does not grant permission to commit against a newer revision. The session always replans the submitted request against its authoritative state.

Keep `stateText()`, `definitionText`, `transitionRecord`/`transitionRecords`, and native session saves as opaque strings when transporting exact logical data. Parsing JSON into ordinary JavaScript numbers can lose integers above `2^53 − 1`. Parsed objects are convenient for UI inspection; forward the original record strings into geometry and persistence APIs.

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

The public `SceneDescriptor`, `MeshAsset`, `VisualPart`, and `HitBinding` TypeScript types describe the wire data. Check `requiredCapabilities` against your renderer before constructing drawing resources. Current capabilities are `triangle-meshes` and `rigid-transforms`. The `diagram` flag identifies flat diagrams. Material bindings are symbolic labels; the renderer chooses colors, lighting, fonts, and selection/blocking styles.

`kernelInfoJSON()` also advertises the `cube-spherical` realization. Its optional scene metadata identifies `ambientSpace: "S2"`, the six `diskCenters`, `sphereRadius`, `diskAngleDegrees`, `fidelity`, and `surfaceTransportVerified`. Mesh assets may declare `rotationSymmetryOrder`; featureless spherical centers use order four. These additive descriptors use the same buffer format and rendering capabilities as other realizations. Custom clients pass a compatible `cube-spherical.json` into the existing geometry constructor; the packaged example data includes both reference cube realizations. See [the spherical construction](spherical-cube.md) for its geometric verification and surface-model scope.

`polyhedral-spherical` intersects catalog prototypes with the unit sphere and uses the existing catalog frames and rotation tracks. The Helicopter package provides twelve 45° disks, seven shared assets, 44 existing pieces, and 48 ports. Local mesh vertices lie on unit shells; the frame includes the uniform display scale, equal to `sphereRadius`. It retains the abstract model's volume, phase, catalog, and exclusion guards; a spherical outline can therefore show tangency for a request blocked by a volume-derived rule. See [the spherical Helicopter construction](spherical-helicopter.md). The copied example data includes `helicopter/helicopter-spherical.json`.

Polyhedral model `ports` map each local port ID to 3–32 distinct model vertex indices. The indices may be unordered; they must describe a coplanar, strictly convex polygon on an exterior supporting plane. The interpreter orders its boundary, triangulates one shared mesh asset, and uses the arithmetic mean of its vertices as the port anchor. Malformed polygons produce `realization.catalog` diagnostics. The experimental [Bagua model](bagua-model.md) demonstrates 146 pieces and 198 polygonal ports using the existing session/planning APIs and Euclidean, diagram and spherical documents.

`PolyhedralModel.surfaceVertices` is an optional array of 4–32 finite numeric triples, consumed by `polyhedral-spherical` only. It specifies the convex hull intersected with the **unit sphere**, in the same local frame as the original model. Omission falls back to `vertices`, preserving existing packages. Original `vertices` and `ports` continue to define port normals, identity and catalog checks; port indices never address `surfaceVertices`. Every declared symmetry must preserve both vertex sets and each labeled port. Malformed surface arrays and incompatible symmetries produce `realization.catalog`; degenerate sections, incompatible disk angles and failed continuous participant/stationary membership produce `realization.spherical`. Every active surface cut must have absolute unit-normal offset `cos(diskAngleDegrees)`.

The [spherical Bagua package](spherical-bagua.md) uses this extension to supply six approximately 66.11° disks and twenty shared assets while retaining all 146 pieces and 198 ports. Its copied document is `examples/canvas/data/bagua/bagua-spherical.json`. These additive schema/type fields retain **API version 1 and buffer version 1**, with unchanged ownership, constructor signatures, capability names, indexed triangles and frame layouts. No Bagua-specific renderer API is required.

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

`kernelInfoJSON().realizationKinds` advertises `articulated-euclidean`, `articulated-port-diagram`, and `articulated-spherical`. API version, document schema version, and frame-buffer version remain 1; these are additive realization capabilities. Clients discover supported kinds before loading documents. Existing kinds and semantic digests are unchanged. The compiler accepts `GearCubeSource` (`kind: "gear-cube-prototypes"`, `schemaVersion: 1`, `semanticsVersion`, `puzzleId`, and a distinct `gearedEdges` subset of the twelve conventional edge IDs). It emits the existing geometry-free placement-relations definition. See [the experimental gear specification](gear-model.md) for its coupling policy.

An articulated document is pinned by `compatibleDefinitionDigest` and has:

- `assets`: a map of IDs to numeric `vertices` triples and integer `triangles` triples. Indexed meshes may be non-convex. C++ expands triangles into the same borrowed position/normal/index buffers as existing realizations; outward winding is authored, normals are computed per triangle. Each asset has 3–65,536 vertices and 1–262,144 triangles; at most 256 assets, 262,144 combined vertices, and 524,288 combined triangles are accepted.
- `componentsByDomain`: exactly every placement domain, each with 1–32 component records. Each component supplies `id`, `meshAssetId`, `role` (`body` or `port`), `placementFrames`, and `operationTracks`. Port components additionally supply `portId`; every local port is bound at least once. Several patches may share a port ID and have different frames/tracks; their unique component IDs produce distinct visual-part IDs, while color labels and hit bindings remain shared. Optional body `materialBindingId` selects appearance. Optional body-only `pieceIds` filters instantiation to a nonempty list of distinct existing pieces in that domain; port components cannot use this filter, so port coverage remains complete. Frames/tracks still cover the whole domain. Optional `symmetries` are at most 32 proper 4x4 label-preserving mesh symmetries, verified against vertices and oriented mesh boundaries/normals. Declaring them requires at most 4,096 asset vertices and 16,384 boundary edges.
- `placementFrames`: exactly every placement key, each mapped to 16 column-major entries of a proper rigid 4x4 transform. Translations occupy entries 12–14. Combined component frame coverage is limited to 262,144 frames.
- `operationTracks`: exactly every operation ID, each mapped to exactly its participating source placement keys. Each path contains up to eight `RotationTrackStep` records: numeric `axis` and `pivot` triples, finite signed `angle` in radians with absolute value at most 100, and `space: "world" | "local"`. Axes are normalized when loading; degenerate axes are rejected. Combined paths are limited to 524,288 rotation steps.

Vertex, axis, pivot, and frame coordinates must be finite and have absolute value at most `Float32.max / 128` (approximately `2.66e36`). This reserves buffer headroom for eight composed pivot rotations and rejects finite doubles that would overflow renderer buffers.

At progress `t`, the interpreter uses `s = t*t*(3-2*t)`. Starting with the source frame, each rotation is applied using angle `s*angle`, in authored order: multiply on the left for world space and on the right for local space. A rotation about pivot `c` maps a point to `c + R*(p-c)`. Thus each primitive rotation is simple in its declared frame while world motion can compose carrier and cog rotation. No matrix interpolation or shortest-path replacement changes a mechanical path. A diagram additionally interpolates translation between chart endpoints; its rotation tracks remain explicit and schematic.

Registration verifies participant endpoints against target frames, up to declared component symmetries. Failures use `realization.articulated`; digest mismatches retain `realization.digest_mismatch`. A malformed recorded transition retains the existing witness diagnostics and does not replace a prepared animation. `prepareAnimationJSON` returns `durationHint` based on the largest participating component's summed angular travel; controllers retain timing ownership. The interpreter never commits logical state.

`articulated-spherical` additionally requires a finite `radius` in `(0,100]`. Asset coordinates already use this display radius; the interpreter applies no extra scale. Every vertex must lie within `radius + 1e-6`, every placement frame must have exactly zero translation, and every world/local rotation pivot must be the origin. These restrictions preserve each vertex's radial distance throughout motion, including inner shells and an internal ball. Invalid values produce `realization.articulated`. The scene reports `ambientSpace: "S2"`, `sphereRadius`, authored `fidelity`, and `catalogTransportVerified: true`. This kind verifies rigid catalog transports; it does not assert `surfaceTransportVerified`, continuous coverage, or gear contact certification. Geometry ownership, borrowing, constructor signatures, errors, and triangle/frame buffer layouts otherwise remain unchanged.

All visual components bind to their persistent abstract `pieceId`; ports retain `portId`. Multiple components do not create duplicate core occupants. The scene and one 16-Float32 transform per visual part use the existing public layout, ownership, borrowing, and disposal rules. Three.js and the independent Canvas/native consumers require no gear-specific kinematics.

The normal Gear Cube's `gear/gear-normal-euclidean.json` contains the supplied model's 38 exterior bodies and one fixed internal ball, using split port patches and a body `pieceIds` filter. It stores meshes in the same JSON format as the other articulated realizations; the original binary model is not needed. These additions change no public methods, semantic digests, buffer layouts, or version-1 documents already supported by the kernel. Clients should update their kernel runtime to load this new realization; rendering still needs only triangle meshes and rigid transforms. The independent package includes this realization under `examples/canvas/data/gear/`, alongside the older twelve-tooth and `*-six-teeth-*` prototypes. Native and WASM clients select it through the existing geometry constructor. Six-tooth tracks preserve the full signed 300° travel per phase step; the legacy twelve-tooth model uses 60°. See [gear authoring](gear-model.md) for storage, shared identity, and contact limitations.

The package also includes each gear preset's `*-supplied-euclidean.json` realization, using the shared normal meshes and the Extreme model's rigid plain edge. Plain edges instantiate one body and two port patches, with identical frames/tracks and no local spin. `gear-extreme/plain-edge-meshes.json` preserves the three source assets; all older Euclidean gear realizations also use these assets for their plain domain. Custom controllers can pair `gear/gear-supplied-euclidean.json` with a compiled custom gear definition by replacing its `compatibleDefinitionDigest` and realization ID, as the catalog domains and transports remain shared. No API, buffer, or schema version changes are needed.

The supplied spherical model is packaged as `gear/gear-spherical.json`, `gear-extreme/gear-extreme-spherical.json`, and `gear-plain/gear-plain-spherical.json` under `examples/canvas/data/`. Its shared source assets are `gear/spherical-meshes.json`. Custom clients may similarly pin the spherical template to a custom compiled gear definition. The model's texture-mask sticker borders are baked into body/port triangles, so rendering requires no GLB loader, texture support, or additional buffer fields. These realizations retain six-tooth 300° cog tracks, plain-edge rigid tracks, and shared abstract identities. The independent native and WASM consumer checks cover all three spherical presets and a custom mask.
