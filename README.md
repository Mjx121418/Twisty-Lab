# Twisty Lab

A portable twisty-puzzle kernel with exact C++ rules and interchangeable C++ geometric realizations. Custom native and browser renderers/controllers use its public APIs. The bundled Three.js application displays synchronized Euclidean, spherical, and labeled port views of the same session.

The initial release includes a 3×3 cube and a reference bandaged cube with the UF edge and UFR corner fused into one rigid piece. It provides explainable blocking, synchronized views, file-based definition inspection, algorithms, reproducible legal scrambles, undo/redo, and portable saved sessions.

The cube presets also support an [ideal spherical realization](docs/spherical-cube.md): six overlapping 70° face disks partition a sphere into the same center, edge, and corner regions. Their larger radius retains the existing pieces. **Cube + sphere** is the default view; choose another pair under **Geometry views**. C++ verifies the disk transports against the existing definition; changing views preserves state and history.

The [flat torus and Klein bottle](docs/flat-surfaces.md) presets use four radius-0.8 cutting disks in a 2×2 square grid, giving 4 face pieces, 8 edge pieces and 20 vertex pieces of two shapes. Their synchronized **Fundamental domain + tiled plane** views show a square viewport and reflected Klein-bottle copies. Drag to pan, scroll in the tiled plane to zoom, and use face keys `A B C D` with Shift for inverse. Displayed copies share the same piece identities and destination choices. The geometric interpreter supplies intrinsic motion; each renderer handles the copies and clipping.

The **Hexagonal torus** and **Hexagonal Klein bottle** use a 60° rhombus with
centers at a vertex, two edge midpoints and its center. These radius-0.8 puzzles
have 4 faces, 12 edges and 8 vertices, with 60° face turns. The synchronized
**Fundamental domain + tiled plane** views show the hexagonal color cells across repeated
copies; the Klein bottle pairs adjacent sides by glide reflections.

An experimental Helicopter Cube preset adds exact jumbling stops, placement-dependent blocking, a complete shape-graph verifier, and synchronized polyhedral, spherical, and port-diagram realizations. Its [spherical interpretation](docs/spherical-helicopter.md) has twelve 45° disks and reuses all 44 pieces, with the existing mechanism pieces visible as uncolored regions. It retains the core's volume-derived legality rules. Its [model specification](docs/helicopter-model.md) records the finite catalogs, geometric interpretation, external acceptance targets, and remaining independent review.

The [Bagua Cube](docs/bagua-model.md) preset adds 146 pieces, exact 45° face turns, placement-dependent blocking, polygonal stickers and synchronized cube, [sphere](docs/spherical-bagua.md) and port-diagram views. It opens in **Cube + sphere**, retaining all existing pieces and ports. Its [verification record](docs/bagua-verification.md) covers published pair/kite cycles, sliver behavior and an independent audit of all cut guards and overlap pairs; whole-model human review and a reproducible physical blocked-state comparison remain open. Select **Bagua Cube**; `+` / `−` buttons turn 45°, while face keys turn 90°.

The [Gear Cube family](docs/gear-model.md) uses the supplied low-poly meshes. Normal Gear Cube has 6 centers, 8 corners, 12 edge bases, 12 six-tooth pinions, and a fixed internal ball. Extreme and custom variants replace each unchecked edge with the supplied rigid [plain edge](packages/gear-extreme/plain-edge-meshes.json). Their default **Supplied model** shares the normal cube's remaining parts; the earlier model choices also use the new plain edge. A supplied spherical assembly provides corresponding geared and plain parts for every configuration, opening in **Cube + sphere** with trackball controls. The C++ interpreter animates carriers and the full 300° local pinion spin; Three.js draws its assets and frames. Euclidean, spherical, and diagram views share state, selection, and history. Continuous spherical gear contacts and coverage remain a [verification goal](docs/gear-spherical-feasibility.md).


## Run locally

Use the existing development container or a compatible environment with C++20, CMake 3.25+, Ninja, Emscripten 6.0.11, Node 24, and npm. Follow [AGENTS.md](AGENTS.md): the container has a 4 GB memory budget, heavy workloads run sequentially, builds now default to one job, and browser tests use one worker. Do not modify `Dockerfile` without an explicit user request. The Bagua authoring check uses bounded caches and a 384 MiB process ceiling; it adds a few minutes to the package check.

```sh
npm ci
npm run build
npm run dev
```

Open `http://localhost:5173`. `npm run dev` also rebuilds the WASM module when needed. `npm run build` checks the prebuilt puzzle data, compiles the WASM engine and TypeScript declarations, and produces the browser application and standalone SDK in `dist/`. It does not build native tools or regenerate puzzles. Use `npm run build:native` when you need the native CLI or native tests.

To serve the production bundle locally:

```sh
npm run preview
```

## Publish on GitHub Pages

Push this repository from macOS, choose **GitHub Actions** as the repository's Pages source, and manually run **Deploy GitHub Pages** when you want to publish immediately, or wait for the daily 00:13 UTC run. Pushing commits does not trigger deployment. GitHub runs `npm run build:web` and `npm run test:publish`, then publishes `dist/`, including the independent Canvas client; no application server is needed. Publishing checks every preset's loading, move, and undo behavior without rebuilding native test tools or repeating the full regression suite. Use `npm run check` for full development validation.

See [the deployment guide](docs/github-pages.md) for initial repository setup, later updates, and testing a repository subpath locally. The workflow discovers the site's base path automatically, so repository names are not hardcoded.

## Using the simulator

The cube presets supply face-turn buttons and face keys `U R F D L B`; hold Shift for an inverse. Choose **Helicopter Cube · jumbling** to see five destination buttons per grip's current phase; its default algorithm `UF_ab UL_af` demonstrates jumbling. Drag the Euclidean view to orbit the camera. Spherical views use free trackball rotation: drag or swipe to rotate along the pointer's direction, stopping on release. Scroll to zoom. The full camera orientation survives view switching and saved sessions. Click a visual part or inspector entry to highlight the same persistent piece in both views. Camera changes leave the logical state unchanged.

Selecting a piece also shows translucent copies of that piece at its legal destination poses in both views. The selected piece has a warm glow and gold outline; destination previews remain faint, and other pieces dim slightly. Click a copy to perform its twist; hovering or focusing a choice emphasizes only that destination. If twists share or overlap a destination, a small chooser identifies the available operations. The **twists** menu inside each view provides keyboard access and choices outside the viewport. Only the selected piece is previewed, without arrows or previews of other moving pieces. The piece stays selected after a move, undo/redo, or view change, and its candidates refresh when playback ends. Escape or an empty-space click clears selection. Previewing preserves state and history; execution checks the candidate's source revision.

Algorithms support inverse and half-turn suffixes, grouped repetition, commutators, conjugates, and line comments. For example:

```text
R U R' U'
(R U)3
[R,U]
[R:U]
# A half turn checks two primitives.
R2
```

Interactive execution keeps the legal prefix and stops at the first blocked primitive. Transactional execution commits the entire algorithm only if every intermediate move is legal. A move such as `R R'` is still blocked on the initial bandaged cube; endpoint cancellation does not make its path legal.

## Headless CLI

The CLI links the core, compiler, and session without the geometry or renderer targets.

```sh
npm run build:native
build/native/twisty inspect --definition packages/cube3/relational-package.json --json
build/native/twisty run --algorithm "[R,U]" --policy transactional --json
build/native/twisty scramble --seed 42 --length 25 --save build/session.json --json
build/native/twisty replay --session build/session.json --json
build/native/twisty verify-fixtures --json
build/native/twisty compile --definition packages/cube3/relational-package.json --output build/definition.json
build/native/twisty validate --definition packages/bandaged/relational-package.json
```

Commands are `compile`, `validate`, `inspect`, `run`, `scramble`, `replay`, and `verify-fixtures`. Use `--json` for compact machine-readable results and `--output FILE` to save an output document. `run` and `scramble` accept `--session FILE` to continue an existing session and `--save FILE` to persist the result. Commands return nonzero for blocked execution or invalid input.

Imported states are checked for declared type and occupancy invariants. This does not certify reachability. Saved histories are replayed under an exact definition digest, and state digests and checkpoints are verified before a load commits. A new move after undo truncates the redo continuation; revisions keep increasing.

## Build a custom front end

```sh
npm run build:kernel
cmake --install build/native --prefix /your/kernel/install
```

Native consumers use `find_package(TwistyKernel CONFIG REQUIRED)` and link `Twisty::kernel` or its individual components. JavaScript/TypeScript consumers copy or locally install `build/kernel/`, which contains a framework-independent WASM/ES-module package and public declarations. The SDK provides state/legality inspection, pure planning, revision-checked commands, history, persistence, geometric assets, sampled frames, and hit bindings. Renderers and controllers own cameras, input gestures, selection, and animation clocks.

See [the public API guide](docs/kernel-api.md), [the independent CMake consumer](examples/native/main.cpp), and [the Canvas renderer/controller](examples/canvas/app.js). Run `npm run check:kernel` to check installed/copied artifacts outside the repository. The production build also serves the standalone Canvas example at `/kernel/examples/canvas/`. The kernel can be built and hosted separately from React, Three.js, and Vite.

The [puzzle language](docs/core-language.md) provides a domain-free C++ executor and JSON compiler. It checks explicit movement tuples against general permission relations; profile selection and bounded discovery are separate helpers. `npm run check:language` compares native and WASM behavior and complete event witnesses, and checks tiny examples against an independent source evaluator. Every production preset uses the current core in the reference application and independent clients. Public API 4 supports schema-2 histories that record movement choices. The legacy evaluator and its migration comparison executables have been removed. The [migration plan](docs/abstract-language-core-plan.md) records the completed migrations and remaining language tooling.

## Authoring and data packages

Every preset has a `relational.pzl.json` language AST, `relational-bindings.json` interpreter catalog, and `relational-package.json` containing both. Realization files pin that package's exact meaning; flat surfaces use `flat-quotient.json`. Gear also has a compact `relational-source.json` wrapper for its portable authoring factory and custom geared-edge masks.

The compiler expands authoring domains to one disjoint placement catalog. It compiles general permission relations and state/goal invariants into shared membership and count nodes. Completion profiles choose local movement tuples outside the abstract executor. The C++ authoring libraries provide cube, bandaged-cube, and Gear families; offline exact authoring generates Helicopter, Bagua and flat-surface tables. No geometry enters the abstract program. See [the relational language](docs/core-language.md) for the language and compact core data structures.

Historical `source.json`, `definition.json`, and original realization pins remain as static authoring catalogs and archived review data. No legacy compiler or evaluator is built. These formats are excluded from the portable package and rejected by the production CLI. Gear's current coupling uses generic occupancy counts and relations to select ordinary versus coupled movement tuples; see [the Gear model](docs/gear-model.md) for geometry and winding.

```sh
build/native/twisty run --definition packages/helicopter/relational-package.json \
  --algorithm 'UF_ab UL_af' --policy transactional --json
```

The letters `a` through `f` identify the six stops. The source phase has three values because the hidden edge is unchanged by a half turn. See the specification before authoring sequences. The browser includes both C++ realizations for this package, so importing its compiled definition also installs the views.

The browser's **Import definition** accepts relational packages, persisted relational definitions, and relational Gear wrappers. A definition with a matching installed realization digest is displayed in both views. Other valid definitions can be inspected and executed headlessly in the browser; missing visual support is reported separately from abstract move blocking. Visual authoring tools and arbitrary executable rule files are outside this release.

Regression fixtures pin a definition digest and specify a replay prefix, request, expected outcome, optional explicit movement choices, and optional blocking evidence or unchanged participant. Update a fixture's semantic pin deliberately when changing its rules. Its source path is an authoring convenience, not a replacement for the digest.

Puzzle authoring is separate from app builds:

```sh
npm run build:puzzles                # Build native authoring tools and regenerate all 11 packages
npm run check:puzzles -- --files-only # Check recorded input and artifact hashes; no compiler needed
npm run check:puzzles                # Also validate schemas, semantic pins, and all WASM realizations
npm run check:packages               # Expensive regeneration-based reproducibility audit
```

`build:puzzles` records `packages/build-manifest.json` only after every generator and semantic-pin check succeeds. The shared inventory in `scripts/puzzle-inventory.json` defines the generation order, authoring dependencies, and runtime artifacts copied into the SDK. Transitive local Python imports and C++ authoring/compiler inputs are hashed along with source meshes and templates. Reference web app code, documentation, and build outputs are excluded. The manifest uses format version 1 with `inputs` and `artifacts` maps from repository-relative paths to lowercase SHA-256 hashes; it contains no timestamps or machine-specific paths. Normal builds and publishing reject stale inputs or altered artifacts and never refresh the manifest automatically.

After changing authoring code or data, run `npm run build:puzzles` and review both the generated packages and manifest before committing. To deliberately accept new fixture identities, run `npm run build:puzzles -- --update-fixtures`; ordinary generation rejects changed fixture pins. Individual Python exporters remain useful during authoring, but finish with `build:puzzles` to validate all packages and record provenance. The shared engine still compiles and validates rules when a puzzle is loaded; offline catalog and mesh generation happens only in the authoring command.


## Build boundaries and memory ownership

Authoring sources live in [`src/authoring`](src/authoring): the language compiler, Cube/Gear generators, and finite-group utilities. Geometric interpreter sources live in [`src/interpreter`](src/interpreter). The minimal executor (`core.cpp`), completion helpers, session and execution adapters, serialization, and native/WASM entry points remain directly in `src`. Public authoring headers live under `include/twisty/authoring`; `include/twisty/core.hpp` exposes the executor. API 4 uses the core/language names throughout C++, WASM, and the SDK; see [the naming migration](docs/kernel-api.md#api-4-naming).

- `puzzle_core`: domain-free state, predicates, sparse transports, and explicit-tuple execution.
- `puzzle_language`: JSON language compiler, completion helpers, and puzzle authoring libraries.
- `puzzle_symmetry`: finite permutation/group/coset utilities.
- `puzzle_catalog`: symbolic interpreter metadata, wire serialization, diagnostics, and digests.
- `puzzle_execution`: validated mapping between the core program and symbolic interpreter/session data.
- `puzzle_session`: notation, revisions, history, legal walks, persistence, and replay.
- `puzzle_geometry`: meshes, placement interpretation, visual identities, animation sampling, and hit bindings.
- `twisty_wasm`: Embind exports with generated `.d.mts` declarations.
- `kernel/`: the public framework-independent TypeScript adapter/types and generated WASM runtime.
- `web/src`: reference-app asset resolution, a Three.js renderer, and the React application.

Definitions and transitions are read-only to realizations. A transition includes all participants, including featureless centers with unchanged placements. C++ animation tracks verify port transport and use a geometric endpoint tolerance of `1e-6`; exact state equality uses no tolerance. Polyhedral realizations validate every catalog transport when loading and account for declared label-preserving mesh symmetries, including hidden edges whose half-turn motion leaves their core phase unchanged.

The Three.js adapter copies borrowed WASM asset views immediately, reuses destination transform arrays, and explicitly disposes WASM and GPU resources. Logical records stay in native serialization paths, preserving exact logical records and presentation values through saves and animation records. JavaScript consumes placement IDs and digests without becoming the state authority.

Compiled definitions load once per session, and both browser realizations share that immutable C++ definition. Compiled membership/count predicates share interned placement sets; snapshots reuse their state and legal-request results within one revision. Every successful move, undo, redo, or load advances the revision. These optimizations preserve canonical definitions, diagnostics, and rule checks while reducing Helicopter loading and input delays. Simulation and rendering run in the browser; the development container serves the application and builds its artifacts.

## Checks

Install Chromium and its container libraries once:

```sh
npx playwright install --with-deps chromium
```

Run the comprehensive development checks sequentially (including the native build and expensive puzzle reproducibility audit):

```sh
npm run check
```

Native checks exercise the current core relations, compiler limits, completion, session ownership, inverses, blocking, replay, all puzzle realizations, flat-surface transport, fixtures, and the retired-symbol boundary. Vitest compares current native and WASM definitions, witnesses, state digests, meshes and sampled frames, published Bagua cycles, serialization, and schemas. `npm run check:language` also checks the portable language API against an independent source evaluator. Kernel-package checks build an external installed native consumer and load a copied WASM package in Node across the supplied puzzles and custom Gear masks. Playwright checks synchronized views, spherical view switching, algorithms, jumbling and bandaging feedback, picking, imports, saves, resource replacement, and the independent Canvas client with one worker. Run `npm run build` before running browser tests individually.

The legacy model-audit and migration comparison executables have been removed with their evaluator. Their recorded findings remain in the model documentation and Git history; they are no longer part of the current regression suite. Production integration tests and installed/copied SDK consumers exercise the distributed kernel directly. Package checks reject retired APIs and verify that every shipped schema resolves without legacy files.

Dependencies are pinned in `package-lock.json`. The nlohmann JSON header is vendored with its MIT license so CMake builds do not need a network dependency fetch. Build and test outputs are ignored by Git.

Independent review of the Helicopter and Bagua models, symbolic placement domains, richer goals, solvers, visual editors, exports, and shared sessions follow the verification gates in [the architecture proposal](twisty_puzzle_simulator_architecture.md). Further portability hardening is deferred while the original puzzle and realization roadmap takes priority.

## Acknowledgments

Most of this project's code, tests, and documentation were written by ChatGPT (OpenAI), working through Codex, with direction, requirements, and feedback from the project owner.
