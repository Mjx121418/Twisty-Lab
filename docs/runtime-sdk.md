# Portable runtime SDK

`@twisty/runtime` executes compiled puzzles and exposes native geometric assets,
frames, exact transition witnesses and revision-checked family controls. Renderers,
cameras, picking gestures and animation scheduling belong to the consumer.
It uses the compiler-free WASM module. Python, puzzle source ASTs and authoring
factories are unnecessary for runtime consumers.

```js
import { createRuntime, RuntimeSession, RuntimeController, GeometryInstance } from '@twisty/runtime';
const module = await createRuntime();
const session = new RuntimeSession(module, {
  abstract: artifactText, controls: controlText, presentation: presentationText,
});
const controller = new RuntimeController(module, session);
const geometry = new GeometryInstance(module, session.native, {
  interpretation: interpretationText, envelope: envelopeText, payload: exactPayloadText,
});
const result = session.run("R U R' U'", 'transactional');
// Completed transition strings, frames and copied mesh buffers feed your renderer.
geometry.dispose(); controller.dispose(); session.dispose();
```

The native equivalent links `Twisty::runtime_kernel` and uses
`twisty::runtime_execution`, `Session`, `Controller` and `GeometryDefinition`.
Configure CMake with `-DTWISTY_BUILD_AUTHORING=OFF -DBUILD_TESTING=OFF` for a native
installation without compiler/factory libraries or authoring headers.
`examples/runtime-native` builds against that installation.

From the repository, `npm run package:runtime` assembles `build/runtime-sdk`.
`node scripts/build-runtime-sdk.mjs --with-presets` also includes compiled sample
data and a standalone Canvas client at `examples/canvas/index.html`. Serve that
directory over HTTP to run the client. The reference web build publishes this
example under `runtime/examples/canvas/`. Preset data is optional in the minimal
package and is generated explicitly with `npm run build:runtime-puzzles`.

The complete `@twisty/kernel` umbrella remains supported throughout this
seven-phase migration. No removal is scheduled; removing its source APIs later
requires a separately documented breaking API change. Its
root and `./runtime` raw-module export preserve existing source workflows;
`./execution` offers this runtime SDK and `./authoring` exposes optional authoring
helpers. Browser source imports and custom Gear construction load that authoring
entry explicitly. New preset sessions save schema 3 with `executionDigest`;
source sessions retain schema 2 with `definitionDigest`. Conversion requires the
exact matching source definition and validates replay before producing a new save.

Runtime API 1 adds controller contract 1 and presentation contract 1. Artifact
API 1, executable schema 1, interpretation 2, envelope 1, witness 2, session 3 and
buffer versions retain their separate meanings. Control documents pin the
execution digest and their own canonical `controlDigest`; phase readers reference
global placement indices and routes explicitly identify compiled completion profiles
and paths. The native reader validates stops, inverse routes, action-scope support,
versions and digests. Presentation metadata supplies inspector labels and grouping;
only native requests and planners determine puzzle behavior.

Native/WASM objects own their contexts. Dispose geometry and controller handles
before sessions. A reset can share an immutable execution context through
`new RuntimeSession(module, existingSession)`. Cross-module sharing fails.
Mesh/pose views returned by raw bindings are borrowed; copy them before another
native call. The high-level geometry adapter owns its copied buffers. Matrices
remain column-major, with 16 float64 intrinsic isometry scalars per visual part;
drawing transforms remain 16 float32 scalars. Space descriptors determine their
geometric meaning. Exact payload bytes must match the envelope's SHA-256.
