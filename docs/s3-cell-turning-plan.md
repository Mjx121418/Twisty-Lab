# S³ cell-turning 8-cell

Implemented model: the 3×3×3×3-style, 80-piece version. The puzzle is the regular spherical {4,3,3}
with eight cubic 3-cells. It has one stereographic view and no port-diagram
realization. The controller has eight cell groups, each containing face-axis,
edge-axis, and vertex-axis twists. The package is `packages/tesseract3`; the portable interpreter implements SO(4)
frames and deformed stereographic drawings. See [the API contract](kernel-api.md#s³-cell-turns-and-stereographic-drawings).

Represent the unit three-sphere by `x²+y²+z²+w²=1`. The eight cell centers are
`±X, ±Y, ±Z, ±W`; a cell consists of the points nearest its center. Thus the
`+X` cell satisfies `x >= |y|, |z|, |w|`. These are spherical cubic cells.

The cut depth is **d = 1/3**. For every center `c`, cut along
`c·p = d` and allow the spherical ball `c·p >= d` to turn. Its angular radius
is `acos(1/3) ≈ 70.528779°`. The interval `0 < d < 1/2` retains this piece
inventory; d controls proportions without changing the combinatorial puzzle.
It is a spherical construction with the usual 3^4 piece inventory, rather than
a radial image of flat subdivision planes on a Euclidean tesseract.

| Piece type | Pieces | Colors per piece | Reachable placements per individual piece |
| --- | ---: | ---: | ---: |
| Cell center | 8 | 1 | 1, with unmarked orientation |
| Square-face piece | 24 | 2 | 48 |
| Edge piece | 32 | 3 | 192 |
| Vertex piece | 16 | 4 | 192 |
| Total | 80 | 216 colored sectors | |

Each spatial position is a nonzero vector in `{-1,0,1}^4`, recording membership
in the eight cutting balls. Every turn involves 27 pieces, including its
center and any other participating pieces whose abstract placement is fixed.
Eight distinct colors label the nearest-cell sectors in the solved state.
These colors stay attached to their pieces during play. The 216 sectors are
three-dimensional portions of the pieces; render their exposed boundaries.
Logical color/port bindings remain useful even though there is no port view.

Use unmarked centers initially. Their cubical rotational symmetries make
orientation invisible; preserve their participation and animation without
adding hidden center-orientation state.

| Axis within one cubic cell | Unoriented axes | Allowed nonzero turns | Operations per cell |
| --- | ---: | --- | ---: |
| Opposite square-face centers | 3 | +90°, −90°, 180° | 9 |
| Opposite edge midpoints | 6 | 180° | 6 |
| Opposite vertices | 4 | +120°, −120° | 8 |

These are all 23 nonidentity proper symmetries of a cube, giving **184 named
cell turns**. Opposite directions along one axis identify the same axis; do not
duplicate it. A half-turn is one self-inverse endpoint operation. Assign stable
operation IDs, explicit inverses, and signed motion paths.

For a cell center c, its tangent space `c⊥` is three-dimensional. Choose an
oriented orthonormal basis B with `det[c,B]=+1`. If A(t) is the ordinary 3D
rotation about the selected tangent axis, use
`Q(t)=c cᵀ + B A(t) Bᵀ` in SO(4). This fixes c and the tangent axis, preserves
the moving ball throughout the twist, and produces an exact signed permutation
matrix at each allowed endpoint. The intrinsic axis is a great-circle geodesic;
its stereographic image can be a circular arc. Computing a Euclidean rotation
around a straight line in the projected scene would give the wrong motion.

The endpoint group is the 192 proper signed permutations of four coordinates.
Its exact action supplies the placement catalogs and transports. Use three
shared moving-piece domains of sizes **48, 192, 192**, plus eight singleton
center domains: **440 catalog entries**, shared across all 80 identities.
Use the current finite-group/action and compact transport support, with
occupancy and color bindings. No geometric predicates or new abstract opcode
are needed. The eight cell groups are fixed spatial grips.

All declared twists are globally legal in this unbandaged puzzle: the cuts
align at every resting state, the moving ball is invariant throughout a twist,
and endpoint rotations permute its partition. Full-state enumeration is
unnecessary. Future bandaging or additional cuts would be separate variants.

The view uses `π(x,y,z,w)=(x,y,z)/(1-w)`, after a presentation-only SO(4)
orientation. The single view ID is **stereographic**, labeled **Stereographic S³**.
Use ordinary 3D orbit and zoom for the projected scene. A separate **Focus cell**
action puts any chosen cell at the projection's central position by changing
the SO(4) view orientation; it changes neither the puzzle state nor history.
Start with discrete focus choices. Free 4D dragging is optional later work.

The cell containing the projection pole occupies the outer, unbounded region.
The reference app keeps the inner and middle layers opaque (53 pieces at rest)
and fades the outer 27 pieces according to their intrinsic centers’ projected
w coordinates. A **Toggle outer layers** button hides or reveals those pieces.
Picking passes through the faded layer. Focus cell makes any cell inspectable
in the central position; the camera frames it and its neighbors.

Pieces contract toward their centers along S³, exposing gaps without leaving
the unit sphere. The piece inset is 0.5375, giving approximately half the
previous linear size. The initial tessellation has 104,544 vertices and
186,624 triangles across 216 colored parts; projected drawings may acquire
extra vertices under clipping.

Stereographic projection is nonlinear. Every evaluated SO(4) pose must be
applied to the intrinsic mesh before projection; projected positions and normals
change during a twist. The renderer therefore needs dynamic vertex buffers.
It cannot animate these pieces solely with the current R³ model matrices.
The projection pole requires surface-aware clipping/subdivision before division;
denominator clamping would distort faces and create giant false triangles.
Expose a finite clip region and report clipped geometry. In the default
cell-centered charts, the pole lies inside a fixed center piece, keeping the
piece boundaries away from the singularity during ordinary puzzle turns.

Implementation order and review gates:

1. **Exact authoring and rule package.** Add `scripts/tesseract_packages.py`
   and `packages/tesseract3/`, with standard source, bindings, package, and a
   single `tesseract3-s3.json` realization. Construct signed permutations with
   integer arithmetic, verify all 184 moves and inverse tables, and preserve
   participating self-loops. Generate eight colors, 80 identities, 216 sectors,
   and the 440 shared placement entries. Publish no global state-space catalog.

2. **S³ geometric interpreter.** Add a capability-gated `s3-cell-turning`
   realization with unit 4-vectors, SO(4) resting frames, and the simple-rotation
   tracks above. Reuse shared piece prototypes and pose tables. Tessellate the
   2D boundaries of the 3D pieces, split them by cell-color sectors, and bind
   every drawing part to a persistent piece and optional sector. Each raw cut
   face is an S² in an affine 3-plane of R⁴, providing a reusable meshing chart.
   Validate radius, orthogonality, determinant, cut membership, endpoint
   transport, color attachment, and declared unmarked mesh symmetries.

3. **Portable projection API and drawing ownership.** Add a stereographic
   projection to `GeometryDefinition::project` and an additive
   `projectWithOptions` entry point for versioned view-isometry, scale, and
   clipping options, preserving the existing two-argument call. Keep projection mathematics
   in the C++ interpreter. An S³ `GeometryFrame` contains column-major Float64
   4×4 SO(4) matrices, explicitly marked non-homogeneous in `GeometrySpace`.
   Extend `GeometryDrawing` to own projected positions/normals and any clipped
   indices and ranges; its R³ drawing transforms can be identities. Retain old
   immutable-asset paths for existing models. Add capabilities and update C++,
   WASM, TypeScript, and `docs/kernel-api.md` together. Keep pose/drawing scalar
   layout versions at 1 if their layouts remain unchanged; any incompatible
   ownership or descriptor change must get its own version, independent of
   puzzle formats and semantic pins. Specify borrowed-view lifetimes and
   snapshot ownership before implementation.

4. **Single projected renderer.** Add the new ambient-space/view descriptors
   and load assets from the drawing. Reuse BufferGeometry/BufferAttribute
   objects while updating changed positions, normals, indices, and bounds.
   Preserve correct outward normals and winding, including the outer shell.
   Picking and selection follow drawing part IDs through projection and
   clipping. Keep Three.js, camera policy, clipping preferences, event timing,
   and gestures in the frontend; the kernel executes only explicit geometric
   evaluation/projection requests. Save focus, camera, and visibility settings
   as presentation data. Do not generate or register a diagram realization.

5. **Eight-group controller.** Provide cell selection, then the three labeled
   categories **Face / Edge / Vertex**, then one of 3 / 6 / 4 axes and the
   appropriate angle buttons. Highlight the cell cap and axis before execution.
   Piece selection can narrow the cell groups containing all selected pieces.
   Use relative turns submitted through the existing revision-checked session
   planner. Rotations about different axes do not commute, so a cell cannot use
   a single accumulated angle or the current one-axis stop ring. Add declarative
   `controlLayout` version 2 metadata with `kind: "cell-axes"`, mapping
   group/category/axis/angle to exact operation IDs, alongside the existing
   version-1 stop-ring layout. Validate the metadata against motion tracks;
   clients never infer an axis by parsing its label. The abstract operation
   family remains the cell ID, so there are exactly eight top-level groups.

6. **Packaging and acceptance.** Add the authoring dependency and the single
   realization to the shared inventory. Generate and validate pins and the
   deterministic manifest through `build:puzzles`. Ordinary app builds and
   publishing consume the prebuilt data. Update standalone native and WASM
   examples to demonstrate projected drawing buffers, including the independent
   Canvas consumer. Run heavy builds and browser checks sequentially within
   the 4 GiB budget, with headless browser isolation and one worker.

Acceptance checks must cover exact piece/sector/catalog counts, all three twist
orders (4, 2, 3), inverse restoration, 27 participants per turn, occupancy and
colored attachment permutations after mixed-axis sequences, and solved/history
behavior. Check intermediate SO(4) orthogonality and cap containment, endpoint
mesh agreement modulo symmetry, stereographic inverse round-trips, outward
normals, clipping across the pole (including triangle interiors), and finite
output buffers. Reject malformed or reflected frames, incompatible pins, and
forged transition witnesses. Compare native/WASM drawing output at start,
middle, and endpoint; exercise all eight groups, three categories, picking,
focus, undo/redo, save/load, and repository-subpath loading in the browser.
Repeated generation must reproduce identical packages, and checks must leave
tracked artifacts unchanged. Record mesh count, load time, projection/frame
time, and browser/WASM memory before setting performance budgets.

Independent planning checks already passed: the 192-element group and all
184 endpoint rotations; 27-piece cap permutations; individual reachability
counts 1/48/192/192; 10,000 sampled sphere points and projection round-trips;
73,600 intermediate rotation samples. These exploratory checks live outside
the repository in `/workspace/math/s3_8cell_checks.py` and
`/workspace/math/s3-8cell-checks.json`. Production checks are self-contained in `tests/tesseract_model_test.py`,
`tests/s3_geometry_tests.cpp`, and the SDK/browser tests. Builds do not depend
on the research directory.

The cube's 23 nontrivial rotations and their face/edge/vertex interpretation
agree with the [MagicCube4D authors' explanation](https://superliminal.com/cube/faq.html).
The spherical construction and SO(4) transport plan above are derived here.
For the projection formula, conformality, and the point at infinity, see
[Schleimer and Segerman, *Sculptures in S³*](https://arxiv.org/html/1204.4952v2).

Implementation validation (2026-10-26): the native geometry/session regression
checks pass; all 184 turns pass from solved and scrambled states. Authoring
checks cover group closure, individual reachability, closed piece surfaces,
colored mesh stabilizers, unit tangent normals, and projection round-trips.
The focused SDK suite passes 33 tests. Installed native and packaged WASM
consumers agree, including sampled deformed S³ positions and normals. Both
publishing smoke tests pass for all 22 presets under `/Twisty-Lab/`; the S³
controller test covers all three axis categories, piece selection, focus,
layer visibility, undo/redo and saved presentation restore. The new package
reproduces byte for byte, and freshness checks leave artifacts unchanged.

A Node/WASM sample in the 4 GiB workspace measured approximately 196 ms for
session construction, 151 ms for geometry construction, and a 15.1 ms median
projection sample across 20 intermediate frames (17.1 ms maximum). The Node
process used about 201 MiB RSS. These include owned drawing-buffer copies but
exclude GPU rendering; they are measurements rather than frame-rate guarantees.
Heavy builds and browser checks ran sequentially with one worker; the observed
workspace peak was below 3.6 GiB.
