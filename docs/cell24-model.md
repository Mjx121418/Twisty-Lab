# Ordinary 3-cell-turning 24-cell

The `cell24` and `cell24-192` presets represent the regular `{3,4,3}` on unit S³, displayed by
stereographic projection into R³. Both use the same GPU SO(4) transform and
projection adapter as the 8-cell. There is one view and no port diagram.

The 24 unit cell centers are `±e_i` and `(±1,±1,±1,±1)/2`. In `cell24`, cutting
balls satisfy `c·p ≥ 2/3`, giving geodesic radius `acos(2/3) ≈ 48.189685°`. Cuts are in the
stable interval `1/√3 < d < 1/√2`. Colors follow nearest cell centers and are
carried with the pieces. Intrinsic meshes use a 56% inset toward their piece
centers, approximately halving their previous linear extent.

| Family | Pieces | Cutting-ball memberships | Colors | Shared placements |
| --- | ---: | ---: | ---: | ---: |
| Unmarked center | 24 | 1 | 1 | 24 singleton catalogs |
| Face center | 96 | 2 | 2 | 192 |
| Edge | 96 | 3 | 3 | 576 |
| Face side | 288 | 4 | 2 | 576 |
| Subcorner | 144 | 5 | 1 | 144 |
| Corner | 24 | 6 | 6 | 576 |

Totals are 672 identities, 2,088 shared placements, and 1,368 colored surface
parts. A placement records the full active cut set and the ordered colored
sectors. Invisible cut memberships are retained for participation. The 576
proper symmetries generate the catalogs; ordinary-turn graph searches reach
every listed placement. Unmarked center, face-center, and subcorner stabilizers
are quotiented without creating visible endpoint jumps.

The smaller `cell24-192` preset uses `d = 1/√2`, exactly 45°. It has 192 movable
pieces plus the same 24 unmarked centers, for 216 identities in total. Only the
first three families in the table have positive volume at this radius: 96
two-color face pieces and 96 three-color edge pieces. The remaining families
collapse onto cut boundaries and are omitted. The cutting balls cover S³;
uncovered vertex regions collapse to isolated boundary points at this limit
and do not form additional pieces.
This preset has 792 shared placements, 504 colored surface parts, and six shared
mesh assets. Its cut surfaces include curved digons, so the mesher joins feasible
small-circle arcs rather than requiring three distinct corner vertices per face.
Zero-area faces are omitted while retaining closed, symmetric piece boundaries.
Its larger patches use six samples per arc and four interior rings, matching
the approximate vertex spacing of the 672-piece model's smaller patches.

There are 24 cell groups. Each exposes four face axes with ±120° turns, six
edge axes with 180° turns, and three vertex axes with ±90° and 180° turns:
552 directed requests altogether. Axis labels use each cell’s local quaternion
frame. Each turns 105 pieces in `cell24` or 21 in `cell24-192`, including self-loops.
All these ordinary turns preserve the complete cut arrangement. Jumbling is
not implemented.

`cell24_model.py`, `cell24_meshes.py`, and `cell24_packages.py` author the exact
abstract catalogs, spherical cut surfaces, and five checked-in package files.
They use only repository modules and Python's standard library. Regenerate with
`npm run build:puzzles`; ordinary app builds consume the prebuilt package.
The corresponding `cell24_192_model.py`, `cell24_192_meshes.py`, and
`cell24_192_packages.py` reuse the exact catalogs and common packaging code for
the smaller variant. Its semantic digest is pinned independently.
The native and WASM APIs advertise `s3-octahedral-cells` and use the additive octahedral S³ realization fields in
[kernel-api.md](kernel-api.md). The 45° variant additionally requires
`s3-octahedral-tangent-cuts`. No application-specific rules enter the core.

Authoring tests check complete participation, reachable catalogs, closed
surfaces, tangent normals, and colored mesh stabilizers. Native geometry tests
exercise every ordinary turn and inverse, animation endpoints, scrambled caps,
all focus charts, and invalid realization rejection. Browser checks cover the
controller categories, focus, GPU animation, and the installed SDK consumer.

The larger compiled catalog needs more memory than the 8-cell. WASM starts at
32 MiB and may grow to a bounded 512 MiB. The 15 shared mesh prototypes use
three samples per cut edge and two interior rings, keeping drawing buffers
smaller while retaining closed surfaces and exact cut membership.

The renderer uses the pieces' positive cut caps to bound both clipping and
opacity. This keeps every one of the focused cell's 105 pieces in the opaque
pass, including half-integral placements. Idle S³ views render again on camera,
selection, projection, or animation changes rather than continuously redrawing
all transparent layers.

Transparent surfaces retain the back/front pass order using cached materials
with fixed sides on the same sortable mesh. Completing a turn installs its
exact committed pose without rebuilding CPU drawing buffers. Picking or a
clipping change refreshes those buffers when needed; ordinary animation and
state synchronization keep using the intrinsic GPU geometry.

The reference view shows the focused cell and its eight face neighbors. A
piece is included when any of its active cuts belongs to one of those cells;
uncolored memberships count too. More distant pieces are fully transparent.
During turns, membership fades continuously between the neighboring alignment
rings. Opacity decreases smoothly with projected radius, from the adjustable
fade-start radius to an adjustable end radius. The latter spans 0.46–1.39
projection units, initially enclosing every neighboring cutting ball (whose
farthest extent is about 1.381 units). The start slider spans 0.45–1.31 units
and stays below the end; reducing the end also lowers the start if necessary.
All 105 focused pieces remain opaque at every setting. Surfaces outside the
end radius are discarded even on triangles crossing the boundary. Both radii
are saved with the camera; changing them updates GPU uniforms without rebuilding
projected geometry. The camera can approach to 0.05 units, with a 0.005 near
clipping plane, allowing inspection from inside the focused cell.

The smaller preset uses the same focus, fading, and camera controls. All 21
focused pieces remain opaque. Its fade-start range is 0.42–1.23 and its
fade-end range is 0.43–1.31 projection units, scaled to the 45° cutting balls.
