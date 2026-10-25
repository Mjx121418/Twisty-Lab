# Face-turning icosahedra

The `icosahedron-110` preset has twenty axes at the vertices of a regular
dodecahedron and angular disk radius
`acos(sqrt((5+2*sqrt(5))/15)) ≈ 37.377368140650°` on the unit sphere. The cuts
form 20 centers, 30 edges and 60 small petals, with no uncovered background.
It opens in synchronized **Icosahedron + Sphere** views.

The Euclidean realization starts as a regular icosahedron. Its conical cuts
extend the spherical boundaries radially inward: `n·x = h|x|`, where
`h=sqrt((5+2*sqrt(5))/15)`. Every piece is the radial sector of its spherical
region, clipped by the regular icosahedron. This is the conical construction
chosen for the 110-piece model. Equal planar face cuts would produce 182
surface pieces. Nonstandard turns produce overhangs in the Euclidean view;
their angular supports stay disjoint throughout motion.

The twenty face colors use nearest-ray sectors. They are flat triangular face
colors in the solved Euclidean model and matching spherical colors in the
other view. Both views retain the same pieces, colors, poses, and moves.
There are 140 colored patches and 110 body parts. The independent Canvas
consumer also loads the Euclidean realization.

## Controls and legality

The axes are named **A–T**, in the deterministic order recorded in
`manifest.json`. Select pieces to narrow the available twist families, choose
a family, then choose its angular destination. Ordinary turns are 120°.
The first jumbling angle is
`alpha=acos((3*sqrt(5)-1)/8) ≈ 44.477512185930°`.

Each driver has nine absolute stops: `120°k`, `120°k+alpha` and
`120°k-alpha`, for `k=0,1,2`. Differences between these stops give fourteen
nonzero signed turn angles. `A` is the ordinary turn about A; `A_j` is its
positive alpha turn. `_inv` names the inverse direction. The other suffixes
identify additional signed angles, shown by the movement tooltip and controls.

`A_j O_j (A_j O_j)'` demonstrates an intersecting jumble and its inverse.
After `A_j`, the neighboring I cut is blocked, while O can turn. Turns,
undo/redo, revision checks, saved replay, and synchronized selection use the
existing portable APIs.

Legality is purely spherical. A turn requires every piece to be wholly inside
or wholly outside its disk, and all participating poses must have verified
transports. There are no hidden mechanical pieces or inherited volumetric
blockers. Connected spherical patches are tested by analytic extrema on their
small-circle boundary arcs and possible interior extrema. The moving disk is
rotation invariant, so a clear cut certifies the complete continuous path.

Logical rest states retain the useful finite alignments. Arbitrary intermediate
angles before the next handoff are not separate saved puzzle states. A cut
that is geometrically clear can still be blocked for a particular destination
when its center phase or a participating transport is incompatible.

## Closed catalogs and state constraints

The three shared catalogs contain **180 center poses, 300 edge poses, and
600 petal poses**, for 1,080 entries total. These are shared by the twenty,
thirty and sixty identities of the respective types. Center identities keep
their original axes. The three ordinary orientations of a uniform center are
equivalent for the solved goal.

The authoring construction uses the sixty proper icosahedral rotations and
three, five, and ten corresponding pose families. It closes the edge catalog
against center-phase compatibility. Each edge pose contains exactly one of
thirty fixed occupancy anchors; every anchor must be occupied once.
Petal closure additionally requires a feasible packing of all thirty edges
and twenty centers, with pairwise spherical overlap constraints. This rejects
the floating placements admitted by a one-piece relaxation alone.

An independent phase check enumerates every relevant boundary-normal alignment
inside a representative cap. All candidate relative angles are among the
fourteen declared angles. For each proposed destination outside the nine
center phases, a finite packing check rules out a locally aligned handoff:
all 162 cases are infeasible. Proper symmetry transports the check to the
other nineteen axes. This is a numerical authoring certificate; no tolerance
or geometry test is used as a runtime abstract rule.

Body symmetries give 60 geometric center locations, 150 edge locations and
600 petal locations. The generated source enforces occupancy, fixed center
identities, and all 7,560 positive-area conflicts between distinct locations.
The compiled core uses existing membership, count, choice and partial-transport
operations. No public API or buffer version changes are required.

## Authoring and validation

`scripts/icosahedron_geometry.py` constructs connected faces, including the
twelve five-circle concurrences at this exact radius.
`scripts/icosahedron_model.py` derives the closed finite catalogs and conflicts.
`scripts/icosahedron_meshes.py` constructs the shared curved patches, flat
icosahedron face patches, and conical walls.
`scripts/icosahedron_packages.py` validates the package with the native compiler
and writes the rules, bindings, two realizations, and manifest.

All inputs are repository-local. No production build depends on the separate
research workspace. The inventory packages both realizations for native/WASM
consumers; ordinary app builds use prebuilt files and run freshness and current
kernel compatibility checks.

Validation covers Euler counts and total spherical area `4*pi`, exact regular
icosahedron exterior area, face-plane and nearest-color constraints, catalog
closure and phase checks, 1,000 geometric moves and their inverses, independent
complete-cut-circle coverage, native execution and geometry endpoints, browser
jumbling/blocking and saved replay, and independent native/WASM consumers.

```sh
npm run build:puzzles
python3 tests/icosahedron_model.py
cmake --build --preset native --target icosahedron_tests
ctest --test-dir build/native -R '^icosahedron_' --output-on-failure --parallel 1
```

## 152-piece variant

`icosahedron-152` uses the same twenty axes and spherical disk radius
`acos(1/sqrt(3)) ≈ 54.735610317245°`. Its 152 connected positive-area regions
have four proper rotational orbits: 20 large triangular faces, 12 pentagonal
corners, 60 wings and 60 small triangular petals. Three disks contain each
petal, four contain each face or wing, and five contain each corner. The
surface has 90 vertices, including thirty four-circle concurrences.

This preset also opens in **Icosahedron + Sphere**. Its Euclidean realization
uses literal **planar** cuts of a regular icosahedron. For inradius `t`, all
twenty face-normal cut planes have offset `t/phi`, where
`phi=(1+sqrt(5))/2`. There are exactly 152 cells with positive exterior area,
with the same disk-membership masks as the spherical regions. The generator
intersects all exterior and cutting halfspaces to construct closed convex
polyhedra; it does not radialize the spherical boundaries. Stickers follow the
original twenty icosahedron faces; the spherical view uses nearest-axis colors.

The chosen rules are **spherical in both views**. Crossing a cutting plane
alone does not prove that a turn collides with a piece: the actual moving
pieces and their complete paths must be checked. For `A_j D`, the planar
pieces are clear at both endpoints, but `corner/A-B-J-K-Q` intersects the
moving `corner/C-D-K-M-Q` around 30° into D. Their original exterior facets
cross, so this is not an obstruction inferred from hidden internal structure.
The Euclidean view illustrates the spherical rules and permits this
intermediate interpenetration. I is blocked by the spherical rules after
`A_j`; O admits an intersecting jumble handoff.

There are no persistent center pieces and no absolute phase drivers. The
reference circle controller maps angular destinations for axes A–T to verified
relative requests through the portable session planner. Piece selection narrows
the currently available axes using verified participants. The gold stop shows
the selected axis's accumulated angle from the session's starting position.
Destinations and direction arcs start at that angle, rather than resetting the
highlight to 0° after each move. This presentation coordinate is reconstructed
from the active recorded moves, so algorithms, undo/redo, reset, and saved replay
all agree. It introduces no phase guard or additional abstract state.
Ordinary turns are
±120°; the fourteen relative alignments include ±alpha and the differences
between useful handoff alignments. The notation `A_j O_j (A_j O_j)'`, undo,
replay and saved sessions work in both views. Uniform large-face orientations
related by 120° count as solved.

The four shared pose catalogs contain 720 wing, 480 petal, 960 corner and
720 face entries: **2,880 poses**. These are shared by identities of the same
shape, rather than repeated per piece. Body symmetries give 720, 480, 192 and
240 geometric locations. The source enforces capacity at each location and
all 40,200 positive-area overlap exclusions. Occupancy predicates and exclusion
neighborhoods share repeated memberships without adding kernel instructions.
All 280 operations retain explicit, verified partial transports.

`scripts/icosahedron_152_model.py` derives the catalogs from a checked geometric
witness walk in `scripts/data/icosahedron-152-witness.json`, expanded by the
sixty proper icosahedral rotations. `icosahedron_152_meshes.py` constructs both
realizations, and `icosahedron_152_packages.py` authors and validates the package.
The witness data is a required authoring input; no build reads the separate
research workspace. A 3,000-turn exploration found no further pose families.
The regression test independently searches geometric handoff angles over
500 further turns, checks full cut-circle coverage, verifies every discovered
transport and inverse, and checks the final return to the solved state.
This is numerical catalog validation, not a proof of exhaustive reachability.

```sh
python3 tests/icosahedron_152_model.py
```
