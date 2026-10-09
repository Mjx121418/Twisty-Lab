# Twisty Lab

A portable twisty-puzzle kernel with one exact C++ rule system and interchangeable C++ geometric realizations. Custom native and browser renderers/controllers use its public APIs. The bundled Three.js application displays synchronized Euclidean, spherical, and labeled port views of the same session.

The initial release includes a 3×3 cube and a reference bandaged cube with the UF edge and UFR corner fused into one rigid piece. It provides explainable blocking, synchronized views, file-based definition inspection, algorithms, reproducible legal scrambles, undo/redo, and portable saved sessions.

The cube presets also support an [ideal spherical realization](docs/spherical-cube.md): six overlapping 70° face disks partition a sphere into the same center, edge, and corner regions. Their larger radius retains the existing pieces. **Cube + sphere** is the default view; choose another pair under **Geometry views**. C++ verifies the disk transports against the existing definition; changing views preserves state and history.

An experimental Helicopter Cube preset adds exact jumbling stops, placement-dependent blocking, a complete shape-graph verifier, and synchronized polyhedral, spherical, and port-diagram realizations. Its [spherical interpretation](docs/spherical-helicopter.md) has twelve 45° disks and reuses all 44 pieces, with the existing mechanism pieces visible as uncolored regions. It retains the core's volume-derived legality rules. Its [model specification](docs/helicopter-model.md) records the finite catalogs, geometric interpretation, external acceptance targets, and remaining independent review.

## Run locally

Use the existing development container or a compatible environment with C++20, CMake 3.25+, Ninja, Emscripten 6.0.11, Node 24, and npm. Follow [AGENTS.md](AGENTS.md): the container has a 4 GB memory budget, heavy workloads run sequentially, builds use at most two jobs, and browser tests use one worker. Do not modify `Dockerfile` without an explicit user request.

```sh
npm ci
npm run build
npm run dev
```

Open `http://localhost:5173`. `npm run dev` also rebuilds the WASM module when needed. `npm run build` produces the native CLI, the WASM module and TypeScript declarations, and a static browser application in `dist/`.

To serve the production bundle locally:

```sh
npm run preview
```

## Publish on GitHub Pages

Push this repository from macOS, choose **GitHub Actions** as the repository's Pages source, and manually run **Deploy GitHub Pages** when you want to publish. Pushing commits does not trigger deployment. GitHub builds the C++ kernel into WebAssembly and publishes `dist/`, including the independent Canvas client; no application server is needed.

See [the deployment guide](docs/github-pages.md) for initial repository setup, later updates, and testing a repository subpath locally. The workflow discovers the site's base path automatically, so repository names are not hardcoded.

## Using the simulator

The cube presets supply face-turn buttons and face keys `U R F D L B`; hold Shift for an inverse. Choose **Helicopter Cube · jumbling** to see five destination buttons per grip's current phase; its default algorithm `UF_ab UL_af` demonstrates jumbling. Drag the Euclidean view to orbit the camera. Spherical views use free trackball rotation: drag or swipe to rotate along the pointer's direction, stopping on release. Scroll to zoom. The full camera orientation survives view switching and saved sessions. Click a visual part or inspector entry to highlight the same persistent piece in both views. Camera changes leave the logical state unchanged.

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
build/native/twisty inspect --definition packages/cube3/source.json --json
build/native/twisty run --algorithm "[R,U]" --policy transactional --json
build/native/twisty scramble --seed 42 --length 25 --save build/session.json --json
build/native/twisty replay --session build/session.json --json
build/native/twisty verify-fixtures --json
build/native/twisty compile --definition packages/cube3/source.json --output build/definition.json
build/native/twisty validate --definition packages/bandaged/source.json
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

## Authoring and data packages

`packages/cube3` and `packages/bandaged` contain compact JSON source definitions, checked-in explicit compiled definitions, and separate Euclidean, spherical, and diagram realization packages. Source definitions use concrete permutations of six abstract face labels, subgroup generators for prototype-position stabilizers, prototype pieces, and a directed operation prototype. C++ validates the stabilizers, enumerates position cosets, and generates finite placements and conjugated operations with their selected-cell guards and source provenance.

Finite definitions can also be supplied explicitly. The native format has piece types, full placement keys, port attachments, cell footprints, directed transport tables, exact mechanism guards/updates, and a home-placement goal. Registered mechanism updates must have exact guarded inverses. Schemas are in `schemas/`; semantic references, occupancy, transport bijections, and inverses are checked by C++.

`finite-placement-relations@1` adds per-placement blocking and partial participant transports, guarded by persistent piece placements. Its resources have capacity at most one, so exclusion pairs can validate catalogs whose placements overlap. `packages/helicopter/definition.json` uses this backend with 240 labeled corner placements, 144 center placements, and 36 hidden-edge placements. The runtime definition contains no geometric data. `scripts/helicopter_model.py` derives it offline using exact arithmetic and emits separate geometric packages with shared models, placement frames, and angular tracks; the package check verifies reproducibility.

```sh
build/native/twisty run --definition packages/helicopter/definition.json \
  --algorithm 'UF_ab UL_af' --policy transactional --json
build/native/helicopter_verify
```

The letters `a` through `f` identify the six stops. The source phase has three values because the hidden edge is unchanged by a half turn. See the specification before authoring sequences. The browser includes both C++ realizations for this package, so importing its compiled definition also installs the views.

The browser's **Import definition** accepts sources or compiled finite definitions. A definition with a matching installed realization digest is displayed in both views. Other valid definitions can be inspected and executed headlessly in the browser; missing visual support is reported separately from abstract move blocking. Visual authoring tools and arbitrary executable rule files are outside this release.

Regression fixtures pin a definition digest and specify a replay prefix, request, expected outcome, and optional blocking evidence or unchanged participant. Update a fixture's semantic pin deliberately when changing its rules. Its source path is an authoring convenience, not a replacement for the digest.

`npm run check:packages` checks the compiled reference files, realization compatibility digests, and fixture pins without changing them. After reviewing a source change, regenerate packages with `python3 scripts/compile-packages.py --write`. Add `--update-fixtures` only when deliberately accepting the new semantic identity, then rerun the checks. Build the native CLI before using this script.

## Build boundaries and memory ownership

- `puzzle_core`: exact states, validation, operations, canonical encoding, and SHA-256 digests.
- `puzzle_compiler`: finite permutation actions, cosets, prototype expansion, and provenance.
- `puzzle_session`: notation, revisions, history, legal walks, persistence, and replay.
- `puzzle_geometry`: meshes, placement interpretation, visual identities, animation sampling, and hit bindings.
- `twisty_wasm`: Embind exports with generated `.d.mts` declarations.
- `kernel/`: the public framework-independent TypeScript adapter/types and generated WASM runtime.
- `web/src`: reference-app asset resolution, a Three.js renderer, and the React application.

Definitions and transitions are read-only to realizations. A transition includes all participants, including featureless centers with unchanged placements. C++ animation tracks verify port transport and use a geometric endpoint tolerance of `1e-6`; exact state equality uses no tolerance. Polyhedral realizations validate every catalog transport when loading and account for declared label-preserving mesh symmetries, including hidden edges whose half-turn motion leaves their core phase unchanged.

The Three.js adapter copies borrowed WASM asset views immediately, reuses destination transform arrays, and explicitly disposes WASM and GPU resources. Logical records stay in native serialization paths, preserving exact 64-bit mechanism values through saves and animation records. JavaScript consumes placement IDs and digests without becoming the state authority.

Compiled definitions load once per session, and both browser realizations share that immutable C++ definition. Occupancy checks use precomputed abstract resource indices; snapshots reuse their state and legal-request results within one revision. Every successful move, undo, redo, or load advances the revision. These optimizations preserve canonical definitions, diagnostics, and rule checks while reducing Helicopter loading and input delays. Simulation and rendering run in the browser; the development container serves the application and builds its artifacts.

## Checks

Install Chromium and its container libraries once:

```sh
npx playwright install --with-deps chromium
```

Then run the checks sequentially:

```sh
npm run check
```

Native checks exercise hashes, canonical compilation, symmetry covariance, inverses, hidden variables, blocked paths, seeded walks, replay, realization endpoints and mesh continuity, spherical disk membership and radial meshes, spherical Helicopter coverage during jumbling, and the exhaustive Helicopter shape graph. Vitest compares native and WASM definitions, witnesses, state digests, Helicopter and spherical meshes and sampled frames, portable fixtures, exact serialization, and schemas. Kernel-package checks build an external installed native consumer and load a copied WASM package in Node, comparing all three puzzles and their spherical packages. Playwright serves the production bundle on port 4173 and checks synchronized views, spherical view switching, algorithms, jumbling and bandaging feedback, camera independence, picking, imports, saves, context/resource replacement, and the independent Canvas client with one worker. Run `npm run build` before running browser tests individually.

Dependencies are pinned in `package-lock.json`. The nlohmann JSON header is vendored with its MIT license so CMake builds do not need a network dependency fetch. Build and test outputs are ignored by Git.

Independent review of the Helicopter model, Bagua, symbolic placement domains, richer goals, solvers, visual editors, exports, and shared sessions follow the verification gates in [the architecture proposal](twisty_puzzle_simulator_architecture.md). Further portability hardening is deferred while the original puzzle and realization roadmap takes priority.

## Acknowledgments

Most of this project's code, tests, and documentation were written by ChatGPT (OpenAI), working through Codex, with direction, requirements, and feedback from the project owner.
