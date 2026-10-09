# Spherical cube realization

Choose **Geometry views → Cube + sphere** or **Sphere + diagram** for the 3×3 or bandaged cube. Both views follow one session, including selection, blocking, algorithms, undo/redo, and saved history. Changing views preserves the exact state and revision. Session presentation saves the selected pair and its cameras.

The `cube-spherical` C++ interpreter constructs an ideal spherical surface cut by six overlapping disks. It consumes the existing cube definition, with the same placement IDs, port attachments, transports, and semantic digest. Three.js draws its shared triangle assets and sampled matrices through the existing renderer interface.

## Six disks and 26 regions

For unit direction `p`, the face disk centered at unit normal `n` is

```text
D(n) = { p on S² : n · p >= cos(alpha) }
```

The six centers are `R = +X`, `L = −X`, `U = +Y`, `D = −Y`, `F = +Z`, and `B = −Z`. The shipped angular radius is 60°, so the threshold is 1/2. Sphere radius 2.05 only sets the display scale.

Opposite disks are disjoint. Every point belongs to at least one disk because the largest absolute coordinate of a unit vector is at least `1/sqrt(3) > 1/2`. Disk membership divides the surface into six single-face regions, twelve adjacent-face pair regions, and eight adjacent-face triple regions. These correspond to the six centers, twelve edges, and eight corners. Shared cut boundaries are interpreted with a numerical tolerance during mesh validation.

Within a region, the face with the largest coordinate assigns its colored port. This subdivides centers into one port, edges into two, and corners into three: 54 labeled ports in total. Labels are attached to persistent pieces; the interpreter obtains their current cell and face from the abstract placement. A bandaged piece binds the five ports of its two constituent regions to one PieceId, retaining the same core footprint guard.

## Moves and verification

A face turn rotates the entire disk about its center normal by the directed quarter turn. Every region whose cell contains that face lies entirely inside the disk; all other regions lie outside its interior. The quarter turn permutes the other four face directions exactly as the existing abstract cube transport does. Since all disks have equal angular radius, their membership conditions agree again at the endpoint.

During registration, C++ requires each operation to select the entire face disk and checks every participating catalog placement against every directed operation. It verifies that its patch lies inside the moving disk, each local port reaches the abstract target attachment, and edge/corner frames reach their target frames. It also checks that the home bindings cover every center, edge, and corner exactly once. The scene reports `surfaceTransportVerified: true` after these checks succeed.

Featureless center placements stay unchanged in the core while their disks turn. Center mesh assets declare fourfold rotational symmetry; the moving mesh reaches the same surface before the interpreter adopts the canonical resting frame. Marked center orientation would require a different abstract definition and realization.

Native tests independently check disk membership and port dominance in multiple reachable states, radial positions and normals, outward triangle winding, stationary surfaces, intermediate spherical motion, every legal primitive in those states, inverse restoration, and actual mesh continuity immediately before endpoints. Native/WASM checks compare assets, scenes, and source/middle/target frames. Browser checks cover the synchronized spherical views, picking, bandage feedback, view switching, and presentation restore.

## Meshes and API

Four local port prototypes cover centers, edges, and the two corner handednesses. Each has a full dark backing surface and an inset colored surface, giving eight reusable assets and 108 visual parts. Their frames are proper rotations about the sphere origin, calculated in C++. No stateful geometry or motion calculation is added to the renderer.

The builder intersects spherical half-space boundaries, includes their exact intersection directions, and tessellates each patch in a gnomonic chart. The default uses 64 angular samples plus boundary intersections, five radial rings, a chart inset of 0.04, and a backing-shell offset of 0.02. The clearance keeps the colored triangle faces outside the entire dark shell, which is checked by the native tests. Vertices and normals are radial; rendered triangles approximate the smooth surface and small-circle cuts.

Realization documents are [cube3/cube-spherical.json](../packages/cube3/cube-spherical.json) and [bandaged/cube-spherical.json](../packages/bandaged/cube-spherical.json). Configurable angular radii range from 55° to 85°, maintaining a margin inside the cube-topology interval `acos(1/sqrt(3)) < alpha < 90°`. Registration also validates radius, inset, shell offset, and tessellation limits. It rejects incompatible definitions or invalid geometric transports.

The public API advertises `cube-spherical`; scene metadata adds `ambientSpace: "S2"`, `diskCenters`, `sphereRadius`, `diskAngleDegrees`, and `fidelity: "spherical-surface-model"`. Buffer version 1 and public API version 1 remain compatible. A custom renderer uses the same positions, normals, indices, transforms, and hit-binding methods described in [kernel-api.md](kernel-api.md).

This realizes the exact cube port action on an ideal cut surface. Solid piece thickness, internal supports, manufactured hardware, and continuous collision certification need separate geometric models and evidence. The Helicopter preset retains its existing realizations.
