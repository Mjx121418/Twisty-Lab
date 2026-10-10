# Normal Gear Cube from overlapping bevel gears

This is a **static construction study** of the normal Gear Cube, following the user's Boolean recipe. The reference simulator's existing moving Gear Cube remains separate while contact geometry and the 38-piece motion catalog are investigated. Gear Cube Extreme is deferred.

Open **38-piece construction ↗** beside the normal Gear Cube's model selector, or visit `gear-construction.html` on the same site. The viewer can show the assembled pieces, isolate any of the 38 identities, explode the assembly, or display one or all of the three overlapping gear groups. It reads assets and frames through the existing C++/WASM interpreter; Three.js handles drawing, picking and camera controls.

## Gear arrangement

All axes meet at the cube center. Each group has two opposed, coaxial large gears with parallel face planes and four small gears whose axes lie in the perpendicular plane:

| Group | Large gears, 20 teeth each | Small gears, 6 teeth each |
| --- | --- | --- |
| U/D | U, D | FR, FL, BR, BL |
| R/L | R, L | UF, UB, DF, DB |
| F/B | F, B | UR, UL, DR, DL |

For example, the UF small gear belongs to the R/L driving group. Its axis is `(0, 1, 1) / sqrt(2)`. Its body lies inside the overlap of the U and F large-gear solids, which belong to the other two groups.

The 20:6 ratio and the 90° angle between a large-gear axis and a small-gear axis determine complementary pitch-cone angles:

```text
delta_big   = atan(20 / 6) = 73.30075577 degrees
delta_small = atan( 6 /20) = 16.69924423 degrees
delta_big + delta_small   = 90 degrees
```

These are derived using the common-apex pitch-cone construction and rolling relationship described in [KHK's bevel-gearing reference](https://khkgears.us/media/rmpacb0n/gear-solutions_khk_standards-in-terminology-for-bevel-gearing.pdf). They provide the pitch surfaces, rather than a manufacturing-ready tooth-flank design.

## Boolean definitions

Let `B_f` be the large bevel-gear solid for face `f`, and `S_ab` the small gear at adjacent faces `a,b`. All solids are clipped to the same outer cube and hollow inner body envelope. Let `c,d` be the remaining opposite face pair: for `a=U,b=F`, these are `R,L`.

| Piece | Construction | Count |
| --- | --- | --- |
| Face U | `B_U minus (B_R union B_L union B_F union B_B)` | 6 |
| Vertex UFR | `B_U intersect B_F intersect B_R` | 8 |
| Normal edge UF | `S_UF` | 12 |
| Extra edge UF | `(B_U intersect B_F) minus (B_R union B_L union S_UF)` | 12 |

The other pieces are proper cube rotations of these four prototypes. The extra edge is one piece spanning two adjacent faces, with its own two colored ports and independent picking identity.

The large-gear tooth crests are placed at `9 degrees + k * 18 degrees`. Each quadrant has three crests assigned to its vertex and two assigned to the adjacent extra edges. The checked angular classification gives **12 vertex teeth + 8 extra-edge teeth = 20 teeth on every large gear**, matching the requested construction. Small-gear crests are at `30 degrees + k * 60 degrees`; the chosen narrow profiles preserve the edge-reflection symmetry.

## Finite depth and the extra-edge pocket

A Boolean recipe for volumes needs finite gear depths. Subtracting a small cone that extends through the entire hollow body can separate the extra edge's two face wings. The small gears here occupy an outer radial band. The extra edge's Boolean complement therefore retains an inner bridge under the pocket, joining the two wings into one connected solid.

Current editable dimensions, in interpreter units:

| Parameter | Value |
| --- | --- |
| Outer cube half-size | 1.48 |
| Hollow body inner radius | 0.92 |
| Small-gear inner radius | 1.78 |
| Tooth angular depth about the pitch cone | ±1 degree |
| Display clearance from Boolean boundaries | 0.008 |

These depths and clearances are modeling choices. They are not measurements of the user's physical cube. Tooth flanks use a tapered trapezoidal angular profile, not a certified conjugate profile. The radial envelope is convenient for the study; axles, bearings, retention features and manufacturing details are not included.

## Mesh generation and evidence

`scripts/gear_compound.py` evaluates the actual implicit intersections and differences, then extracts each prototype with bounded marching tetrahedra. One prototype is meshed at a time, with no CAD dependency or unrestricted parallel work. The extra-edge pocket uses a finer grid because a coarse grid falsely disconnected two small tooth fragments.

The generated [review record](../packages/gear-compound/review.json) checks:

- 12,000 deterministic directions, finding all 38 angular regions and no sampled raw-pinion overlap with another piece;
- all 20 tooth crests on each large gear, obtaining the expected 12/8 allocation;
- 14,400 points around a pinion rim, finding no sampled intrusion into the remaining large-gear pair;
- a single connected component, positive signed volume and exactly two incident triangles at every undirected edge of each prototype mesh.

These are solved-pose and mesh checks. They do not certify all points, all intermediate twists, backlash, tooth contact, or physical manufacturability. In particular, intersection-derived rigid pieces need a separate continuous-motion audit before the static construction becomes the moving simulator's model.

Run `python3 scripts/gear_compound.py --audit-only` for the inexpensive angular study, `--write` to regenerate accepted meshes, or no flag to verify package reproducibility. The normal assembly and the unpartitioned parent gears have separate exact definitions and geometric realizations under `packages/gear-compound/`. Both load through the existing native and WASM APIs, and are included in the independent kernel package.

## Motion integration next

The assembly has 38 independent abstract identities in its study catalog, with no advertised twist operations. This avoids assuming that the previous 26-piece motion catalog already accounts for the new extra edges. The next step is to derive their placement transports and rigid-carrier motion, pair each small gear with its extra-edge carrier, and check contacts over complete primitive paths. The expected carrier motion has no local cog spin; the small gear retains its signed 300° spin for the 20:6 driving step. All such rules remain exact and geometry-free in the abstract layer.

The assembly's body meshes, face ports and transforms use the existing articulated buffer layout. No kernel API or buffer-version change is needed. Custom renderers can inspect the same construction without loading the reference application.
