# Spherical Bagua realization

Select **Bagua Cube · experimental**. It opens in **Cube + sphere**; **Geometry views** also offers **Sphere + diagram** and **Cube + diagram**. All views share the existing session, selection, blocking evidence, undo/redo and saved replay. Drag the sphere with the existing trackball controls; its rotation follows the pointer and stops on release.

The package [bagua-spherical.json](../packages/bagua/bagua-spherical.json) uses the C++ `polyhedral-spherical` interpreter. Three.js consumes shared mesh assets and sampled frames through the existing [kernel API](kernel-api.md). The abstract definition and its semantic digest are unchanged: there are still **146 pieces, 198 ports, 2,232 labeled placements and 30 directed operations**.

## Construction

Simply intersecting the Euclidean Bagua pieces with the unit sphere would miss some triangle-center pieces and leave holes around the interior hardware trims. Instead, sample a sphere of radius **10/9** in the original cut coordinates and normalize its coordinates by **9/10**. Extend the outer cube faces to tangent planes of the unit sphere. The original layer depth `9/20` becomes:

```text
d = (9/20) × (9/10) = 81/200 = 0.405
|p| = 1
faceAxis · p = d = cos(66.10887285919112°)
```

Six disks are centered on the signed coordinate axes. Incident-face diagonal cuts become `±x ±z = √2 d`, with coordinate permutations; their unit normals also have offset `d`. The displayed sphere has radius 2.05, a uniform visual scale that leaves these angular cuts unchanged.

The center-block trim becomes `y >= 0.81`, and the representative edge trim becomes `x + z >= 1.215`. The omitted portions lie strictly inside the sphere: their largest squared radius is bounded by `0.81² + 2 × 0.405² = 0.98415 < 1`. They therefore contribute no surface boundary. The surface prototypes retain all incident cut constraints, including constraints that were redundant for a trimmed Euclidean piece, and omit these interior trims.

The eight prototype types produce the same solved partition: eight corners, six centers, 24 triangle centers, 24 triangle edges, 24 slivers, 24 left kites, 24 right kites and twelve edges. Original port normals divide multiport pieces into their colored regions. All existing pieces and ports have nonempty surface regions; none are added to the core.

Each model's optional `surfaceVertices` describes its normalized surface hull. Its original `vertices`, `ports` and `symmetries` retain the Euclidean port identities and placement contract. This distinction is necessary because the spherical extension can change which exterior facets a prototype touches. The interpreter uses the surface hull for S² and the original model for port metadata and catalog validation.

Twenty shared mesh assets supply 146 dark body parts and 198 colored port parts. C++ constructs plane/sphere intersections, tessellates local charts and supplies radial normals. Small sticker insets and a backing shell provide cosmetic clearances. The triangle meshes approximate the curved mathematical regions.

## Motion and legality

Every placement frame and operation track is a proper rotation about the origin. Thus `R(P ∩ S²) = (RP) ∩ S²`, and the existing placement transports give exact spherical targets. Registration verifies all catalog transports and all participant/stationary disk memberships using continuous extrema, including circle intersections, boundary extrema and interior stationary points. It also requires every active prototype cut circle to agree with the declared disk angle and every declared symmetry to preserve both model vertex sets.

In the solved state, the retained cut regions partition S². A legal move rotates all its participating regions inside one disk while its stationary regions stay outside. Rotation about that disk's axis preserves the disk, so coverage and separation persist during accepted turns. The native check independently tests this using prototype inequalities rather than exported hulls or mesh vertices.

Legality remains inherited from the [experimental Bagua abstract model](bagua-model.md). Volume-derived cut blockers, exact catalog restrictions and overlap resources remain authoritative. This realization does not establish that spherical outlines alone explain every blocked request or exhaust every possible motion of a physical spherical mechanism. The outstanding whole-model human review and physical blocked-state replay requirements in [the verification record](bagua-verification.md) still apply.

## Verification

The native check verifies all piece/port bindings, mesh radii, normals, winding, colored-triangle clearance, independent coverage, stationary frames, near-endpoint continuity and exact inverses. It exercises every legal primitive at several states of a deterministic jumbled walk, checks unchanged session/history, retains blocked-request rollback, and rejects incompatible cuts, malformed surface vertices and broken surface symmetries.

Native/WASM parity compares scenes, meshes and sampled frames through a jumbled sequence and its inverse. Installed native and copied WASM consumers load the packaged spherical document independently of the reference application. Browser tests cover the default pair, picking, shared state, inherited blockers, inverse restoration, view switching and saved presentation.

```sh
ctest --preset native -R bagua_spherical_realization
npm run test:unit
npm run check:kernel
npm run test:e2e
```
