# Configurable Gear Cube

The **Gear Cube family** uses the supplied low-poly models, converted into the project's existing triangle-mesh JSON format. The normal model was created in a separate ChatGPT conversation; the complete Extreme model adds a plain edge. The original GLBs and their importers are not retained. This replaces the previous Boolean-construction investigation and viewer.

All three presets use the domain-free C++ executor through relational packages. Generic membership/count predicates express their interactions; no Gear-specific guard handler runs in the core. Each variant provides one Euclidean model, one spherical model, and one six-phase diagram. These views share a session; **Cube + sphere** is the default pair. The gear checkboxes select which persistent edges have a cog; changing a checkbox creates a new solved definition and resets history. The configuration applies to piece identities, not fixed locations. Extreme, all plain, and custom configurations share the normal cube's supplied six-tooth parts and use the supplied plain edge for unchecked pieces.

Production realization documents are pinned to their compiled definitions. API 4 accepts relational packages and schema-2 saves; legacy sources and schema-1 histories are unsupported.

## Exact rules and configuration

A `relational-gear-prototypes` source has format version 1, a puzzle ID, and a `gearedEdges` array containing any distinct subset of `UF UR UB UL DF DR DB DL FR FL BR BL`. The native/WASM authoring library generates an ordinary language AST and interpreter bindings, which compile to the same persisted relational definition as the packaged authoring check. No coordinates, floating-point angles, meshes, or camera data enter the abstract definition.

There are 26 persistent pieces: eight corners, twelve edges, and six centers. A geared edge is one composite identity; its carrier and cog are visual components, not duplicate core occupants. Geared placements retain a carrier orientation and one of six exact cog phases. Geared and plain placements claim the same edge-location occupancy resources. Imported assignments cannot place a carrier assembly and a plain edge in the same slot. Featureless center orientation is quotiented, with unchanged participating centers retained in transition witnesses.

The **any-geared-middle-slice-v2** policy chooses coupling from current placements:

- `Fq` is legal when the middle layer contains no geared edge. It turns only the front layer 90° clockwise. Geared edges on that face ride with it without local spin.
- `F` is legal when at least one middle-layer edge is geared. It turns the front face 90° clockwise and the opposite face −90° about the same world axis. Middle-layer carriers, plain edges, and centers stay fixed; each geared middle edge advances one signed abstract cog phase. Its rotation angle belongs to the realization.
- Mixed geared/plain middle layers permit the coupled request. Exactly one forward request, and its inverse, is legal for every face in every valid configuration. Inverses use an apostrophe; numerical suffixes repeat primitives.

For a solved cube with only UF and UR geared, Uq/Dq are ordinary and R/L/F/B are coupled. After pieces move, legality is recomputed from their new locations. The request relation requires 1–4 geared middle edges for coupling and zero for an ordinary turn, then permits the corresponding tuple of local movements. The compiler lowers these conditions to shared membership/count nodes and exact transports. Controllers do not maintain a separate coupling state.

Opposite coupled requests are identical complete motions: `R = L`, `U = D`, and `F = B`, including their signed cog phases and animation winding. Each driven middle cog advances phase by +1 modulo six for a forward request and −1 for its inverse, regardless of carrier port ordering, slot, or existing phase. The realization binds positive phase to a right-handed rotation about the cog's **outward radial axis**. Swapping the two carrier ports changes the carrier frame but preserves that radial axis; it must not reverse the cog. With only UF geared, R and L therefore spin UF in the same direction. Ordinary requests such as Rq and Lq remain separate single-face turns.

The direction follows the two bevel-gear contacts. Let `a` be the requested face's outward axis and `n` the middle cog's outward radial axis, perpendicular to `a`. The requested face has angular direction `−a`; its contact velocity points along `−a × n`. A positive pinion spin has velocity `n × a` at that contact, which points the same way. At the opposite contact, both velocities reverse together. This determines the handedness independently of port labels or the authored phase tables. The six-tooth model retains the agreed full 300° travel, rather than using a shorter rotation with an equivalent endpoint.

Face keys choose a legal geared or ordinary request at dispatch; explicit notation retains the distinction between `F` and `Fq`. These rules cover all geared masks explicitly. They do not establish that every mask corresponds to a manufactured mechanism. The model remains **experimental**, including the contact geometry and cog-spin convention. Shape-derived phase-dependent collisions and independently reviewed physical constraints are further verification gates; additional placement exclusions can be expressed through the same generic occupancy predicates without adding geometry to the runtime core.

[Jaap's Gear Cube analysis](https://www.jaapsch.net/puzzles/gearcube.htm) provides the relative outer/middle coupling and twelve-turn behavior. The current opposite-face convention is equivalent to the earlier 180°/90°/0° motion after removing a 90° whole-cube rotation. [Jim Storer's firsthand description](https://www.cs.brandeis.edu/~storer/JimPuzzles/ZPAGES/zzzGearCube.html) records the 60-degree cog increment. [Jaap's Extreme analysis](https://www.jaapsch.net/puzzles/gearcube2.htm) provides the eight-geared/four-plain reference and ordinary turns along the plain slice. These are factual references, not borrowed implementations. Published total state counts use additional visual and whole-puzzle quotients; the explicit carrier/phase state has not been equated with those totals.

Examples:

```text
Gear Cube:          R U F (R U F)'    R12
Gear Cube Extreme:  Uq R Fq (Uq R Fq)'
All plain:         Rq Uq Fq (Rq Uq Fq)'
```

## Articulated geometric interpreter

The reusable C++ interpreter supports `articulated-euclidean`, `articulated-spherical`, and `articulated-port-diagram`. An authored mesh can be non-convex, allowing a toothed cog. Every rigid component has a resting frame for each placement and an ordered list of simple rotations for each participating operation/source placement. Rotations specify an axis, pivot, signed angle in radians, and `world` or `local` space. World rotations multiply on the left; local rotations multiply on the right. All steps use one shared eased progress value, so local cog rotation accompanies carrier motion continuously. Inverse tracks preserve direction and winding.

Registration checks rigid frames, mesh indices, complete domain/port bindings, participant-track coverage, and endpoint agreement for every declared transport. Declared label-preserving mesh symmetries allow featureless center orientations. The diagram uses explicit schematic translations between chart positions and local marker rotation to expose cog phase. It is not a mechanical motion model.

Three.js draws supplied assets and per-component matrices, using the same selection, picking, destination preview, and history controller as existing puzzles. It contains no gearing calculations. Custom renderers use the same portable API and buffer layout. See [the API contract](kernel-api.md).

## Supplied Gear Cube family models

The supplied model had five shared geometry prototypes: 8 corners, 6 centers, 12 edge bases, 12 six-tooth pinions, and 1 internal ball. The exterior has the requested 38 physical bodies. Each extra edge base and its pinion share one persistent abstract edge identity and occupancy resource, preserving the existing 26-piece exact definition and saved sessions.

The supplied triangles and colored surface regions are stored in [normal-meshes.json](../packages/gear/normal-meshes.json) without reshaping. Its 13 assets use the existing `vertices` and `triangles` representation, separating body and color patches. During conversion, a proper global rotation mapped `(x,y,z)` to `(x,z,-y)`, placing white at U and green at F; a uniform scale of 1.48 matches the reference application's cube size. The home assembly was compared against every source surface and color region before removing the binary. Source provenance, including its SHA-256, is recorded alongside the meshes.

The updated outer edge base from the later supplied complete Extreme model replaces `gear-edge/carrier` and its two `carrier-port-*` patches. Each base now has 174 plastic-body triangles and 14 triangles per colored patch. The normal cube and supplied Extreme/custom realizations share this revision; pinions, plain edges, other parts, and rotation tracks are unchanged. The mesh provenance records this update's source SHA-256 separately from the original model.

`scripts/gear_normal.py` combines these assets with the existing placement-frame and rotation-track authoring and emits [gear-normal-euclidean.json](../packages/gear/gear-normal-euclidean.json). The original converted assets stay unchanged; the emitted cubic realizations now use the symmetry remeshing described below. Loading uses the ordinary public native/WASM geometry APIs and triangle buffers. There is no separate binary-model loading path. The interpreter computes its usual flat triangle normals, and face labels use the frontend's palette.

Each edge base and its colored patches follow the carrier frame. Each six-tooth pinion and its colored patches additionally spin 300° (5π/3) relative to that frame per coupled move, using the agreed 20:6 ratio through a 90° driving step. Both motions share the interpreter's eased clock; inverse tracks reverse the full signed travel. A local port can have separate patches on the base and pinion, keeping one color label and hit binding despite their different motion. The internal ball is a single frozen body component bound to `center/U` for picking; it has no separate abstract identity and does not rotate with the center. Its body-only `pieceIds` filter prevents duplicate copies.

The abstract catalog has six phases: `phase × 300°` modulo 360° gives six resting frames for a fixed carrier orientation. Six signed phase steps complete five full cog revolutions. Every realization pins its regenerated definition digest. Select **Animation → Inspect (3 s)** for direct browser review. The source explicitly sets `motion_clearance_verified: false`; neither the import nor endpoint checks certify continuous tooth contacts or physical clearances.

The supplied Extreme model has 8 corners, 6 centers, 8 edge bases, 8 six-tooth pinions, 4 `fixed_edge` bodies, and the internal ball. Its other 13 mesh assets match the normal model exactly. All surfaces and color regions in the reconstructed Extreme home assembly were compared with the complete source before removing the GLB.

The new plain piece is stored in [plain-edge-meshes.json](../packages/gear-extreme/plain-edge-meshes.json): one plastic body (162 vertices, 164 triangles) and two colored patches (12 vertices, 10 triangles each). Its canonical UF coordinates use the proper rotation `(x,y,z) -> (-z,y,x)` and the same 1.48 scale. Color patches are bound to the abstract edge's two local port labels. A plain edge has exactly these three rigid components; it has no separate carrier, pinion, bearing, or local phase spin. All three components share the same placement frame and layer-rotation tracks, so they move together through both ordinary quarter turns and applicable coupled quarter turns of the opposite faces.

`scripts/gear_normal.py` combines the shared normal parts with this plain-edge prototype to produce `gear-extreme-supplied-euclidean.json` and `gear-plain-supplied-euclidean.json`. The reference frontend reuses the Extreme template for every custom mask, pinning it to the custom compiled digest. Which pieces use the plain domain is determined by persistent abstract identity, so a plain piece retains its mesh as it moves to a new slot. The Extreme preset has 34 exterior bodies and one ball; an all-plain cube has 26 exterior bodies and one ball. Both retain 26 abstract identities.

The earlier procedural models and their model selector have been removed. New saved sessions record view selection and cameras without a `gearModel` setting; any obsolete presentation preference in an existing session is ignored. This model cleanup changes no abstract rules, semantic digests, or move history. Continuous tooth contact and clearance are still unverified.

## Cubic tessellation symmetry

`scripts/gear_cubic_mesh.py` remeshes all supplied cubic exterior prototypes, excluding the internal ball. It uses the shared sector clipping, coplanar simplification, reflection-winding handling, and connectivity audit in `scripts/gear_symmetry_mesh.py`. For each piece it chooses the least complex equivalent source sector before copying it. `scripts/gear_mesh_reduction.py` then removes redundant vertices in complete symmetry orbits, including subdivisions along the sector joins. A vertex on a symmetry axis is removed only if its replacement respects the subgroup fixing that vertex. Original converted sources remain available for reproducibility. The source's slight asymmetries are regularized, so the facets and patch borders can differ slightly from the original import.

Corners repeat under threefold rotations and three mirrors (S3), with corresponding port permutations. Centers repeat under quarter turns and mirrors (D4). Plain edges, geared carriers, and cubic cogs repeat under end reflection and adjacent-face reflection (D2); exchanging faces exchanges their two port labels. The cubic cog has two perpendicular flat face surfaces, so its complete shape has D2 symmetry. The spherical cog's sixfold geometry uses a different surface realization. Neither meshing changes the abstract orientation/phase catalog.

The cubic color patches remain flat on their original lifted face planes. Their rims and the black band along the cog's central edge are copied as whole surface regions, keeping continuous boundaries without replicating separator fragments around the teeth. Tooth outlines, face bindings, resting frames, and full signed rotation tracks are retained. The ball's vertices and triangles are identical to the converted source.

The fixed central ball uses smooth outward radial normals in both cubic and spherical views. Its original 162 vertices and 320 triangles remain intact. The cubic realization marks all ball triangles with `radialNormalTriangles`, which the interpreter now supports in every articulated ambient space. Other cubic surfaces retain their flat shading, and independent renderers receive the smoothed ball through the normal buffer.

Further reduction is limited to gently curved, inward-facing surfaces. Their cumulative deviation from the symmetry-copied mesh is bounded by **0.006 display units**, about 0.2% of the cube's width. The bound compares heights over every old/new triangle-intersection polygon, including edge crossings, and carries forward previous reduction error. These operations keep boundary vertices and material borders; flat patches may only lose geometrically redundant subdivisions. Outward surface areas and first moments agree with the unreduced symmetry mesh to numerical precision. The reduction is performed during package authoring, with no runtime cost. The previously approved spherical meshes are unchanged by this optimization.

| Shared prototype, including colored patches | Supplied triangles | Symmetry-meshed triangles |
| --- | ---: | ---: |
| Corner | 319 | 321 |
| Center | 40 | 48 |
| Plain edge | 184 | 154 |
| Geared edge carrier | 202 | 200 |
| Six-tooth cubic cog | 224 | 240 |
| Central ball | 320 | 320 |
| **Total, including plain-edge template** | **1,289** | **1,283** |

The assembled normal cubic view uses **8,456 triangles**, down **12.6%** from the first symmetry mesh's 9,680. Extreme decreases from 8,480 to **7,312** (13.8%), and all-plain decreases from 6,080 to **5,024** (17.4%). The original imported normal assembly used 8,224 triangles; the default cube-plus-sphere pair is now within 1% of its count before cubic symmetry remeshing. The authoring step audits every triangle's symmetry and transported label and limits shared prototype counts to at most 3% above the source, alongside the geometric deviation limit. Independent native checks compare rotated/reflected triangle connectivity and winding, unchanged ball geometry, and shared plain-edge assets across presets. Renderer APIs and kernel buffer layouts are unchanged; ball shading uses the extended radial-normal option described above.

## Supplied spherical model

The supplied `gear_extreme_s2.glb` provides a spherical Extreme assembly: eight corners, six centers, eight carriers, eight six-tooth pinions, four plain edges, and an internal ball. Its six shared prototypes become sixteen body/port assets in [spherical-meshes.json](../packages/gear/spherical-meshes.json). Cube symmetry supplies all twelve geared positions for the normal preset; the same geared and plain prototypes support Extreme, all plain, and custom masks. The exterior body counts remain 38, 34, and 26 respectively, with one additional internal ball and the same 26 abstract identities.

The source's outer surface vertices lie near radius one, with inner shell vertices near 0.945 and an internal ball near 0.925. The conversion applies the same proper orientation used for the Euclidean model and a uniform scale to display radius 2.05. The plain-edge prototype uses the corresponding proper canonical edge rotation. Triangles approximate the curved surface by chords. Colored source regions use a texture atlas with dark sticker borders; the conversion clips those regions into disjoint colored port and dark body triangles on the original triangle planes. Atlas contours use a 1.5-pixel simplification tolerance. The center's square sticker and cap tessellation are made exactly quarter-turn symmetric for the core's featureless-center orientation quotient. Source SHA-256, body counts, transforms, and these conversion choices are recorded in the JSON provenance. The original binary and one-time importer are not retained.

Before removing the binary, the reconstructed Extreme home assembly's surface areas, first moments, and signed volumes were compared with all 35 source bodies. Non-center bodies agree within `1e-8`; the deliberate center retessellation changes these quantities by less than `3e-5` in display coordinates. Conversion also checked that splitting each colored source triangle conserved its UV area.

`scripts/gear_spherical.py` combines these meshes with the existing placement frames and full signed 300° cog tracks to produce the three `*-spherical.json` documents. Spherical component frames have zero translation and every rotation pivot is the sphere origin, preserving radial distances during animation. The reference application uses its existing trackball controls, selection, destination preview, undo/redo, and saved-view settings. Switching to **Sphere + diagram** also works. This addition changes no abstract definitions, rules, semantic digests, or saved logical states.

Spherical shell triangles use smooth radial vertex normals, while tooth and cut walls retain flat normals. Authoring identifies shell triangles by their radial bands, including chord-interpolated sticker-border vertices, and records `radialNormalTriangles` in each realization asset. This classification is invariant under the piece rotations; registration checks the centers' quarter-turn shading symmetry. The C++ interpreter supplies these normals through the existing portable buffer. The initial smoothing change retained all **10,258 prototype triangles** and their original geometry. No subdivision or per-frame normal calculation is required.

The subsequent symmetry remeshing in `scripts/gear_spherical_mesh.py` clips one fundamental sector from each exterior prototype, removes redundant coplanar subdivisions within that sector, and copies its triangles by rotations and reflections. It reverses triangle winding for reflected copies and transports the colored port labels. Thus the vertex positions and triangle connectivity repeat under the symmetries, including inside the patches. Corners have threefold rotations and three mirrors (S3); centers have fourfold rotations and mirrors (D4). Plain edges and geared carriers have end reflection and adjacent-face reflection (D2). The six-tooth cog's combined geometry has sixfold rotations and mirrors (D6). Its two colored halves have D2 symmetry with port exchange; sixfold geometric symmetry does not erase their distinct colors or the abstract phase. The central ball's positions and triangles are unchanged.

The cog's tooth outline comes from the source sector's walls, while its shell and color layout are meshed separately. Its outer and inner caps use three radial bands on the original shell radii. A geometric inset supplies an even black rim, and a continuous straight band separates the two colors in gnomonic coordinates. All three rotated separator bands cut the triangle layout, preserving sixfold connectivity; only the band between the actual colored halves is painted black. This removes the four stray rim tips and pinched center caused by copying texture-seam fragments. The rim inset is 0.016 and the separator half-width is 0.011 in those coordinates, recorded in realization provenance. The generator samples both sides and the interior of the band to check continuity.

The large plain edge used by Extreme has smooth, nearly straight boundaries beside the corners, following the cubical reference. Each side is a straight line in edge-centered gnomonic coordinates, which projects to a great-circle arc on the sphere. Its width leaves a minimum 0.018 clearance from the adjacent corner's outer surface in those coordinates, avoiding an overly broad gap. This removes the source's side step and bulge while retaining its center-facing notch. The colored patches follow the sides with a 0.016 inset and a 0.011 separator half-width; both shell radii and D2 symmetry are preserved. These shared plain-edge assets also apply to all-plain and custom configurations. The generator checks both straightness and corner clearance in the resting geometry; continuous motion clearance remains unverified.

The normal cube's outer edge carrier uses the same center-facing tip, width, notch, and colored border as the large plain edge. Each short side is one uninterrupted great-circle arc, with its cog-facing profile extended to meet that side. Both pieces share the tip authoring and border offset, so their physical and visible gaps beside the common center piece agree. The generator independently compares both center-facing profiles across their full widths and checks that the short sides have no intermediate bends. Carrier caps use radial normals and retain D2 symmetry and the original shell radii.

Each piece uses fewer triangles after remeshing:

| Shared prototype, including colored patches | Supplied triangles | Symmetry-meshed triangles |
| --- | ---: | ---: |
| Corner | 3,197 | 1,068 |
| Center | 172 | 160 |
| Plain edge | 2,918 | 992 |
| Geared edge carrier | 867 | 576 |
| Six-tooth cog | 2,784 | 1,056 |
| Central ball | 320 | 320 |
| **Total** | **10,258** | **4,172** |

The generator audits every triangle under these symmetries, checking connectivity, winding, and transported port labels. It also rejects triangle-budget increases relative to the supplied source. The supplied source remains unchanged for reproducibility. Selecting a common sector regularizes its slightly asymmetric approximations, and the authored edge caps are projected onto their spherical shells, so the resulting facets and sticker borders can differ slightly from the original conversion; the source-conservation comparison above applies to the original converted assets. Motion tracks and smooth-normal handling are preserved. The assembled normal preset contains **29,408 triangles**, down from **70,740**, including the ball.

The supplied mesh is available for browser review. Its catalog transports and radial motion are checked; continuous surface coverage, tooth contact, and clearance remain unverified. See the [spherical verification goals](gear-spherical-feasibility.md).

## Verification

Tests cover fully geared, plain, Extreme, and mixed configurations; twelve-turn closure; inverses; occupancy; dynamic mixed-slice legality; opposite-face motion; stationary middle carriers; shared component identity; and animation endpoint continuity. WASM checks verify outward cog motion on every face before and after a scramble, including the solved F/UR regression. Native/WASM parity compares assets and sampled frames for every current Gear realization. Geometry generators verify mesh symmetry and triangle budgets. The external native and JavaScript consumers load articulated realizations without the reference application. Browser checks cover configurations, blocked and legal requests, synchronized animation, selection, history, spherical presets and custom masks, saved view restoration, and the normal F/UR phase and undo. These checks establish transport, rotation handedness and travel; they do not certify continuous tooth contact or clearance.

Run `python3 scripts/gear_normal.py` to check the supplied Euclidean realizations, including their tessellation symmetry; use `--write` to regenerate them from `normal-meshes.json` and `plain-edge-meshes.json`. `python3 scripts/gear_spherical.py` checks the spherical realizations against `spherical-meshes.json`, including their full tessellation symmetries and triangle budgets, also accepting `--write`. `python3 scripts/gear_model.py` checks the phase diagrams and shares placement/rotation templates with the mesh generators. All three run in `npm run check:packages`; finish authoring changes with `npm run build:puzzles` to refresh the manifest.
