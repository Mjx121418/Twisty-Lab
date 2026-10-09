# Spherical Helicopter realization

Select **Helicopter Cube · jumbling**, then **Geometry views → Cube + sphere** or **Sphere + diagram**. Both views share the existing abstract session, move controls, blocking evidence, selection, undo/redo, and saved replay. Switching views preserves state and history.

The package [helicopter-spherical.json](../packages/helicopter/helicopter-spherical.json) uses the C++ `polyhedral-spherical` interpreter. Three.js draws its shared triangle assets and sampled rigid transforms. Custom frontends use the same public [kernel API](kernel-api.md); no renderer-specific geometry or rules enter the kernel.

## Construction on S²

The straight-cut model has twelve edge axes, proportional to the permutations of `(±1, ±1, 0)`. Its cut planes are `n · x = 1`, with `|n| = √2`. Intersecting these planes with the unit sphere gives twelve small circles:

```text
|p| = 1
(n / √2) · p = 1 / √2 = cos(45°)
```

Each moving disk therefore has a 45° angular radius. The visible radius is 2.05 display units; this scales the unit construction uniformly and does not change its angular cuts.

For each original, unshrunk prototype polyhedron `P`, its spherical body is `P ∩ S²`. The outer cube faces become redundant tangent bounds. This construction retains all **44 existing pieces**: eight corners, 24 face centers, and twelve mechanism pieces. The mechanism pieces, hidden inside the Euclidean puzzle, become uncolored surface regions. No pieces or placements are added to the core.

The original pieces partition the cube outside a central rhombic dodecahedron. That central region lies inside the unit ball and touches its sphere only at six axis points. Consequently, the spherical sections cover S², with shared boundaries at those points and the cut circles. Colored corner regions are divided among their three existing ports by their declared face normals; each face-center region has its existing single port. There are still 48 labeled ports.

C++ finds exact plane/sphere intersections numerically, constructs a local hemisphere chart, and tessellates each section along its circular boundaries. Seven assets supply 44 body parts and 48 colored port parts. Radial normals, a small port inset, and a separate backing shell provide cosmetic clearances. These finite triangle meshes approximate the mathematical curved surfaces.

## Motion and inherited legality

Every catalog frame and operation track is a proper rotation about the origin. For such a rotation `R`:

```text
R(P ∩ S²) = (RP) ∩ S²
```

The existing exact placement transports therefore supply spherical resting frames and continuous rotations. C++ verifies every catalog transport and labeled port binding, including the mechanism model's half-turn symmetry. It also computes continuous extrema on each section from interior stationary points, boundary-circle extrema, and circle intersections. Every participating section must lie inside its moving disk, and every stationary section must lie outside it.

A disk is invariant under rotation about its own axis. Starting from complete coverage, rotating exactly its participant sections preserves coverage and separation throughout each accepted move. The existing six angular stops, persistent mechanism phases, and partial catalog transports remain authoritative.

**Legality is inherited from the existing abstract model.** Intersecting a volume with S² can turn a volume-crossing blocker into a tangency. Thus a spherical outline alone does not explain every blocked request: the core still applies its volume-derived blockers, mechanism guards, catalog restrictions, and exclusion resources. This is a spherical realization of the current Helicopter state machine, not a claim that its legal requests exhaust those of a mechanism defined solely on S² or match a manufactured Helicopter Ball.

## Verification

The dedicated native check verifies shared assets, radial shells and normals, winding, colored-triangle clearance, stable picking, independent surface coverage at resting and intermediate states, stationary pieces, near-endpoint continuity, half-turn mesh symmetry, exact inverses, and unchanged session state. It exercises every legal primitive at several deterministic walk states and rejects incompatible cuts, display scales, and tessellation parameters.

Native/WASM parity compares assets, indices, scenes, sampled frames, and final state digests through jumbling and its inverse. Browser checks cover synchronized Euclidean/spherical views, picking, blocked requests, inverse restoration, view switching, and saved-session replay. An independent installed native consumer and copied WASM package also load this realization.

```sh
ctest --preset native -R helicopter_spherical_realization
npm run test:unit
npm run check:kernel
npm run test:e2e
```
