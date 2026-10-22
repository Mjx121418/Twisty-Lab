# Face-turning flat surfaces

There are square-grid and hexagonal-grid presets for both surfaces. All four
share the abstract language, quotient interpreter and renderer; the new grids
introduce no core rule.

The flat torus and Klein bottle presets use the square `[0,2) × [0,2)` with four
unit squares. The cutting disks have radius `4/5` and centers A `(0.5,0.5)`,
B `(1.5,0.5)`, C `(0.5,1.5)`, D `(1.5,1.5)`. A named turn is 90° clockwise in
that center's fundamental chart; the inverse and half turn are also available.

The full circle arrangement gives 32 pieces: 4 face regions, 8 edge regions,
16 vertex lobes, and 4 vertex cores. A lobe is inside three disks; a core is
inside four. These are two shapes within the vertex family. At this radius
all vertex regions can move. The four initial unit squares supply the colors.
An asymmetric L marking on each piece makes its orientation and handedness
visible even where its color is uniform.

## Identification and motion

Both surfaces identify horizontal translations `H(x,y) = (x+2,y)`. The torus
also uses `V(x,y) = (x,y+2)`. The Klein bottle instead uses the glide reflection
`V(x,y) = (2−x,y+2)`, with `V H V⁻¹ = H⁻¹`. Thus its top/bottom pairing reverses
horizontal direction. There is no global clockwise orientation on that surface.

The offline authoring generator uses integer half-unit coordinates and signed
permutation matrices for placements and quarter-turn transports. Canonical
placement frames record chart orientation, including parity on the Klein bottle.
The abstract program contains no coordinates, circles, or renderer logic. It
uses the existing language, placement maps, completion profiles and occupancy
predicates without a new core rule or catalog kind.

Geometric motion is a rotation about the appropriate lift of the requested
center. The interpreter validates its endpoint modulo the identifications.
Sampling an intermediate frame retains a continuous lift; the final frame is
the exact canonical target. A renderer's complete set of images makes this
canonicalization visually continuous. Source placements near different seams
can use different lifted centers, but no displayed tile is generated in C++.

## Interpreter and renderer boundary

`flat-quotient` realizations reuse planar indexed assets and rigid component
frames/tracks. `flatSurface` declares `topology`, `sideLength`, and optional
`cutRadius` and `diskCenters`. The scene exposes `ambientSpace: "E2"` and:

```text
flatSurface.topology                 torus | klein-bottle
flatSurface.fundamentalBounds        [left, bottom, right, top]
flatSurface.deckGenerators          [horizontal 4×4 matrix, vertical 4×4 matrix]
flatSurface.twoSided                 true
flatSurface.cutRadius                optional
flatSurface.diskCenters              optional map of names to [x,y]
```

Frames and generators are column-major. A planar reflection is lifted to a
proper 3D rigid frame by also reversing z. All assets lie at local z=0, so
renderers must draw both sides. Mesh buffers and one-frame-per-visual-part
ordering remain version 1. The interpreter emits each physical part once and
never includes camera, viewport, tiled-copy or clipping state.

The `quotient-surface` rendering capability means the client can apply these
identifications, draw two-sided planar meshes, clip them to its chosen chart,
and resolve displayed images to their original visual-part IDs. A reflected
image naturally reverses apparent rotation when its matrix is composed with
the sampled frame; it needs no additional per-copy angle rule.

The reference Three.js renderer offers a fixed square/rhombus fundamental-domain
window and a tiled plane filling its rectangular panel. Both are shown by
default. The **Views** list lets users hide either view, reorder them or restore
the default selection. Dragging moves the
orthographic camera over the covering plane. The fundamental window stays
anchored on screen, with zoom disabled; loading a saved camera also restores
its zoom to one. Its GPU clipping planes follow the camera, and picking uses
the matching world-space window. The tiled plane supports pan and zoom, with
no additional polygon clipping or viewport outline. Neither view draws
fundamental-domain seams or arrows. Disk labels move with the contents.

Fixed pools of 25 copies for the fundamental window and 81/117 copies for the
square/hexagonal tiled planes cover the supported zoom range plus animation
padding. The tiled camera fits a bounded portion of the plane to fill any
panel aspect ratio, including single-view layouts. Deck addresses follow the
camera as it crosses chart cells, permitting continuous panning without
accumulating meshes or pieces.
Destination previews, highlights and hit bounds follow the recycled copies.
All images select the same piece. Destinations group by abstract target
placement rather than an image's displayed pose. Both views support history
and saved cameras. Neighboring images are copies, not new pieces.

The standalone Canvas example implements the same square presentation using
only the public buffers, identification matrices and Canvas clipping.

## Hexagonal presets

`flat-hex-torus` and `flat-hex-klein-bottle` use the rhombus with side vectors
`a=(2,0)` and `b=(1,√3)`, an acute angle of 60°. The four cutting centers are
A `0`, B `a/2`, C `b/2`, D `(a+b)/2`: one vertex, two edge midpoints, and the
center. Their lifts form a triangular lattice of spacing one, whose Voronoi
cells are regular hexagons. The radius is 0.8. The arrangement has 24 pieces:
4 face regions, 12 two-disk edge regions and 8 three-disk vertex regions.
The hexagonal Voronoi cells supply the initial colors.

`A B C D` turn 60° clockwise in the named center's chart. Prime reverses a
turn; `A2` and `A2'` turn −120° and +120°, and `A3` turns 180°. Six basic
turns return the exact state. Integer coordinates in sixths of the triangular
basis give exact C6/D6 placements and transports. Three shared models, color
patches and L markings use 236 unique triangles, sampled on a common 96-angle
grid. Body and color-patch mesh connectivity respects their shape symmetries.

The torus glues opposite sides by translations `a,b`. The Klein bottle glues
**adjacent** sides: bottom to right by `G(x)=Sx+a`, left to top by `Sx+b`, where
`S` swaps the rhombus basis vectors (reflection in the long diagonal). In
Cartesian coordinates `S=[[1/2,√3/2],[√3/2,−1/2]]`. This is an isometric gluing
with no fixed points. All four corners identify with total angle 360°. Its
deck group has generators `T(x)=x+a−b` and `G`, satisfying
`G T G⁻¹=T⁻¹`; `G²` translates by `a+b`. The smallest glide displacement is
`√3`, so radius 0.8 is below the global injectivity bound `√3/2`.

For a displayed lattice tile `(i,j)`, the deck map is translation `i*a+j*b`
after `S^(i+j)` on the Klein bottle, or just translation on the torus.
Consequently neighboring rhombi alternate handedness. The interpreter still
emits 100 physical visual parts once, with continuous intrinsic motion. The
Three.js and independent Canvas renderers compose the image matrices. Their
fundamental-domain views clip against the actual rhombus, including destinations
and picking. The Three.js tiled plane fills its rectangular panel.

The additive `flatSurface.chart: "rhombus-60"` authoring field enables this
chart. Its scene adds `fundamentalPolygon` (counterclockwise Cartesian points),
`latticeBasis: [a,b]`, and, for the Klein bottle, `tileReflection` (a proper
column-major 4×4 lift of `S`). `fundamentalBounds` is only the enclosing box;
fundamental-domain clients clip and pick against the polygon. The two `deckGenerators` are
`[T,G]` for the Klein bottle and translations `[a,b]` for the torus. No image
list, viewport or camera is supplied by the interpreter.

Run `python3 scripts/hex_surface_packages.py` to verify these packages, or add
`--write` to regenerate them using the native compiler.

## Reproduction and limits

`python3 scripts/flat_surface_packages.py` checks the committed packages; add
`--write` to regenerate them using the native compiler. Analytic shared circle
arcs use a common 64-angle lattice plus exact intersection endpoints. The
maximum chord error is below 0.001 unit. Four shared piece models, their color
patches and markings use 210 unique triangles in each realization. Centroid fans
preserve mesh connectivity under each body's symmetries; the authoring check
verifies those symmetries for both body and color-patch triangles. The L marks
deliberately distinguish orientation and therefore break the visual symmetry.

The presets fix radius 0.8 and four cutting centers. Changing radius
across a tangency changes the piece layout and requires new authoring. Embedded
donut/Klein-bottle views and arbitrary fundamental polygons are deferred; the
supported charts are the square and the 60° rhombus.

## Pillowcase presets

`flat-pillowcase` and `flat-pillowcase-6` realize the `2222` orbifold, the
quotient of the side-2 square torus by `I(x,y)=(1-x,1-y)`. Its fundamental
domain is `|x-1|+|y-1|≤1`, a square of side √2. Pair the halves of each
boundary edge by a half-turn about its midpoint. The four midpoints A–D
become order-2 cone points; the four vertices identify to one regular point.

The four-disk preset uses radius **0.846** and has 4 cone-center pieces,
4 edges, 8 lobes, and 2 cores: **18 pieces**. Its completely marked state
space has 384 states. The six-disk preset uses radius **0.6** throughout,
adding E at `(1,1)` and F at the identified vertices, represented by `(1,0)`.
It has 4 cone centers, 2 ordinary faces, 8 edges, 16 lobes, and 4 cores:
**34 pieces**. Its lifted cuts form a square grid rotated 45° and scaled by
`1/√2`. All six cuts are compatible with 90° lifted rotations.

The solved coloring follows the nearest cutting center. For four disks, the
lines `x=1` and `y=1` divide the fundamental square into four equal isosceles
right triangles, colored A–D. For six disks, write
`(x,y)=(1+u-v,u+v)` with `0≤u,v≤1`. Four lines at `u,v=1/4,3/4`
give one large central square colored E, four rectangles colored A–D, and
four small corner squares colored F. All four corners have the same color
because they meet the same identified center. The color patches belong to
the pieces and travel with them during turns; they are not fixed chart overlays.

The A–D turns have order 2; E and F have order 4. Prime reverses the
animation path even when the endpoint agrees with the forward turn. A full
180° cone turn has an identity endpoint and still animates. Both presets
use the same fundamental-domain and tiled-plane controls as the smooth
surfaces. Adjacent square images carry a 180° rotation. Picking all copies
of a cone or the shared vertex selects the same physical piece. Cone
center assets cover half their planar lift, with matching markings in
the other half, so copies introduce no doubled surfaces or false cut gap.

The scene requires `quotient-orbifold` and adds the three deck generators,
`chartOrigin`, `tileHolonomy`, and `conePoints` described in the
[public contracts](kernel-api.md). Viewport state and image pools remain
outside the kernel. Reproduce the two committed packages with
`python3 scripts/pillowcase_packages.py`; `--write` deliberately updates them.
Use `npm run build:puzzles` afterward to validate all packages and record
their manifest. Native and WASM tests check the move orders, quotient geometry,
coloring, and the four-disk preset's 384-state graph.
