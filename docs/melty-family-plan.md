# Melty family implementation plan

Status: Melty A–D are implemented for the declared 85°, d=1/3 cube models.
`scripts/melty_model.py` reconstructs the analytic rays and reviewed placement
recipes; `scripts/melty_packages.py` generates the prebuilt package and all three
rigid realizations. The checked-in authoring inputs live in `scripts/data/`;
generation and runtime have no dependency on the ignored research directory.
Each package has 27 piece-specific domains, including the fixed internal core.

Each family supplies Euclidean, spherical, and port-diagram realizations. The
default presentation is **Cube + sphere**; the diagram remains available in
the view picker. The spherical model intersects the same six cutting halfspaces
with the unit sphere, then displays the result at radius 2.05. Its six axes,
rotation frames, and signed motion tracks are identical to the cubic model's.
Angular Voronoi sectors of the six directed rays color the solved sphere: each
point takes its nearest ray's corresponding cube-face color. The colored patches
then move rigidly with their pieces. Smooth radial normals shade the small-circle
mesh patches; an inner black shell and
inset colored patches provide visible piece boundaries. The fixed core stays
inside the sphere. Logical legality still follows the cubic solid model, so a
cut can look unobstructed on the sphere while being blocked by the full solid.
Adding this view does not change rule packages, definition pins, or session
formats. All three realizations are included in the independent SDK.

Before display scaling, each spherical surface patch is a subset of its
unshrunk cubic piece. A clear volumetric driving cut therefore cannot slice a
spherical participant or stationary patch. The same origin-centered rotation
preserves both the sphere and the driving disk throughout a legal twist.

The cube is centered at the common ray origin and oriented to preserve each
family's complete ray symmetry. A and D use the near-face-center choice of the
two isolated orientations, with their threefold axis on a cube body diagonal.
B puts its half-turn axis through opposite edge midpoints; C uses a diagonal
cube mirror plane. Their remaining twist minimizes the angular RMS distance
from each ray to its corresponding face normal, rather than the largest angle.
The resulting RMS angles for A/B/C/D are approximately
7.558829° / 7.578041° / 8.157399° / 9.381828°. All six rays meet distinct face
interiors. The package manifest records `cubeOrientation`, including the chosen
policy, ray directions in the author's z-up cube frame, and angular metrics.
The realizations convert this frame to the application's y-up convention.

The orientation is a proper rotation of the analytic construction, preserving
ray angles, chirality, placement IDs, and phase recipes. Piece bodies, stickers,
cut blockers, and complete-solid exclusions are regenerated relative to the
fixed cube. Existing placement witnesses and geometric catalog-coverage audits
are replayed against these new solids; the original research coordinate frame
does not determine the cube's current orientation.

| Family | Catalog placements | Witnessed placements | Witness nodes | Directed requests |
| --- | ---: | ---: | ---: | ---: |
| A | 2,320 | 1,466 | 6,818 | 72 |
| B | 1,474 | 1,208 | 766 | 48 |
| C | 2,721 | 1,682 | 10,587 | 76 |
| D | 4,303 | 2,020 | 1,667 | 108 |

The current stop rule is **middle-aligned single caps**. The driving cut must
be clear. At the endpoint, some intersecting cut must align across the moving
cap and stationary middle; the opposite cap may obstruct that full cut. We do
not require another complete twist to be immediately available. The same local
alignment is checked at the source to keep inverse requests well-defined on
imported states; it holds throughout reachable paths.

Opposite caps remain disjoint inside the radius-√3 ball containing every
rotated piece. Therefore old simultaneous opposite-cap turns can be performed
as two single-cap turns, in either order. All four packages expose six families,
with no paired requests. Each cap creates its own undo/history entry. Principal
turns remain about 85.401744°, except C's D grip at about 89.709219°; the
controller shows the actual discrete angular stops.

The previous production catalogs kept only completed handoffs. Removing that
restriction needs additional intermediate placements, including a concrete C
path which escapes its old catalog. The new frozen `*-middle-catalog.json`
inputs preserve every old placement and rotation ID as a prefix. They were
selected from the existing finite phase/anchor construction using a relaxation
that tracks one piece and all six center phases. Other pieces' collisions and
blockers are omitted during this projection, so candidate placements are a
conservative superset, not a claim that every entry is reachable. Original
witnesses cover 1,466 / 1,208 / 1,682 / 2,020 placements; remaining entries are
marked `reachabilityWitnessed: false` and listed in each manifest. This numerical
evidence is not a formal completeness proof or an extension to arbitrary
continuous stops.

Every old witness graph is replayed with paired nodes split into legal single
steps. Native tests also reverse their order, check inverses, session replay,
and geometric endpoints. A regression permits D's `U D_e_inv` endpoint even
though no intersecting full cut is free. Complete-solid collision exclusions
remain unchanged in meaning: an exact bipartite factoring reconstructs every
original forbidden pose pair before emitting shared IN/RANGE predicates.

The reference app and standalone Canvas SDK load prebuilt rules and rigid
geometry. A uses port bindings; B–D share identical articulated local movements
only when complete transports and signed paths agree. Shared rotation tracks
and transport selectors avoid dense per-request geometric path catalogs. No
abstract opcode, API version, buffer layout, mesh complexity, or continuous
rotation direction changes are needed for the new stop rule. Generation and
native compilation run in separate processes within the 4 GiB budget.

Rule-revision checks: all four native witness suites pass, including 1,769 old
paired nodes checked in both single-turn orders. `python3 tests/melty_middle.py`
audits 3,000 sampled turns per family and 84,443 geometrically permitted
destinations without a catalog escape. It visits six additional C placements.
The separate C regression preserves a concrete 42-turn route to a destination
missing from the old production catalog. Driving-cut straddling remains blocked
in regression fixtures for all four families.

At the middle-alignment checkpoint, before the later [anchor refactor](reference-anchors.md),
removing paired requests and factoring solid exclusions reduced the serialized packages:

| Family | Previous package bytes | Current package bytes |
| --- | ---: | ---: |
| A | 5,197,871 | 3,241,842 |
| B | 2,670,397 | 1,826,165 |
| C | 5,417,935 | 4,696,489 |
| D | 9,611,396 | 9,093,436 |

D's standalone language source is 8,124,687 bytes, below the existing 8 MiB
source limit. No compiler budgets or WASM heap settings were increased.

The following sections record the original investigation. Counts referring to
4,480 / 2,583 / 4,981 / 7,471 are the broader guarded authoring universes,
before the current projection. New parameters still require a separate
feasibility audit; do not silently truncate an unbounded placement space.

## Model to implement

Six directed unit rays n_i determine six rotation axes through the origin. Cuts
are planes n_i · x = d_i. A move rotates all complete pieces on the outer side
of one plane; any piece straddling that plane blocks the move. Rotations may
change the outer shape.

Store the ray geometry, cut depth, and outer shape explicitly in authoring
configuration:

| Parameter | Initial proposal |
| --- | --- |
| Outer solid | Literal cube [-1,1]^3 |
| Cut offsets | All d_i = 1/3; a research choice, not a measured physical dimension |
| Ray systems | Equal-angle A, then D, then B and C |
| Common special angle | 85° |
| Axes | The six fixed rays through the origin |
| Pieces | The complete cells produced by the six planes; no extra cuts |
| Availability | No complete placed piece straddles the proposed cutting plane |
| Stops | Derive actual aligned handoffs from the current transformed cut faces |
| Colors | Six original cube-face colors, with stable piece/port identities |

Use the four equal-angle ray arrangements from the inventors'
[geometry discussion](https://twistypuzzles.com/forum/viewtopic.php?f=1&t=33164).
Record chirality explicitly. Internal construction is outside the model.

## Findings that constrain the implementation

The local numerical investigation reconstructs the rays from equal-dot-product
constraints. At the proposed parameters all four systems partition the literal
cube into 26 exterior pieces and one internal core: 6 one-face pieces, 12
two-face pieces, and 8 three-face pieces. The core is not a 27th colored piece.
Other parameters can change this topology.

The computed ray-system symmetry groups are A: C3v, B: C2, C: Cs, D: D3.
B and D are chiral. These need not be symmetries of a literal cubic shell in an
arbitrary orientation. In particular, do not infer central inversion for D;
the [inventor's correction](https://twistypuzzles.com/forum/viewtopic.php?f=15&t=33893)
also rules it out.

An 85° inter-axis angle does not mean an 85° move. Initial plane-normal matching
on A gives seven candidate positions on three grips and five on the other three,
including zero. One nonzero angle is approximately 85.401744°. This agrees with
the first-phase position counts in the [Melty A discussion](https://twistypuzzles.com/forum/viewtopic.php?f=15&t=33825).
Every tested initial candidate permits an overlapping next grip in the ideal
model. Ordinary 90° endpoints leave overlapping neighboring cuts blocked.

First-turn results and random walks alone do not establish closure. The follow-up
uses exhaustive contact and center-phase exclusions, a reduction covering paired
partial turns, exact algebraic rotation identities, and conservative enclosure
audits of source geometry, endpoint enumeration, and solid overlap constraints.
The catalogs retain 4,480 / 2,583 / 4,981 / 7,471 entries for A / B / C / D,
including the core. Shared exact rotation frames number 723 / 406 / 1,027 / 1,194.
The rounded published A coordinate table fails intended equal-angle matches;
generate rays from constraints rather than importing that table with loose
tolerances. Changing ray angle, depth, or outer solid requires new certification.

## 1. Establish a certified authoring model

Add `scripts/melty_model.py`, using the convex clipping and pose-catalog patterns
in `scripts/helicopter_model.py` and `scripts/bagua_model.py` where applicable.
Their rational and quadratic number fields are not direct substitutes for the
85° construction.

- Generate A–D rays from constraint-preserving formulas, with a stable ordering,
  branch/chirality choice, and serialization. Certify unit lengths, nine common
  angle pairs, and the intended near-antipodal pairings.
- Intersect all cut halfspaces with the declared cube. Retain exact or certified
  convex cells, original exterior-face ports, cut-face provenance, and the core.
  Verify positive volume, disjoint interiors, and complete coverage.
- Derive each piece's actual rotational symmetries from its body and labeled
  ports. Different edge/corner shapes may require different prototype domains;
  six centers must not automatically receive square-center equivalences.
- Choose an algebraic representation or adaptive interval predicates that can
  distinguish identities from small nonzero differences. Report uncertainty
  instead of using matrix rounding as a placement identity.

Completion: deterministic baseline geometry and certified predicates, plus
documented parameters and mechanism assumptions.

## 2. Port verified stops and closure into production authoring

For a turn about n_i, let m be the current world normal of any actual cut face
on a participating piece. Candidate matches with another grip n_k require
n_i · m = n_i · n_k and matching plane offsets. Their angles follow from the
oriented angle between the perpendicular projections of m and n_k onto n_i's
normal plane. Filter candidates against every complete piece after the turn.

The original cube's inscribed ball stays completely filled under all legal
origin-centered turns. Each interacting pair of Melty cuts overlaps inside
that ball, so a new free cut must coincide with an actual moving cut face.
Consequently, this face-based method captures every isolated handoff from a
fixed state; matching normals alone is still insufficient to establish legality.

- Derive subsequent candidates from the actual transformed cut faces;
  do not repeat the solved-state angle list as an assumed universal move table.
- Classify each placed piece by its complete solid relative to a grip plane:
  moving, stationary, or blocking. Preserve partial alignments that allow
  jumbling; do not require a permutation of all six global cut planes.
- Preserve the verified per-piece placement closure and coupled center phases.
  Avoid enumerating the entire puzzle state space. Track geometric provenance
  for every discovered pose and move. Apply blocker and phase restrictions during
  discovery: unrestricted group closure of rotations about the six axes can be
  infinite even when the legal puzzle has a finite placement catalog.
- Establish that every allowed source/endpoint maps each participant to a known
  placement and that no aligned handoff introduces an unrepresented phase. Use
  certified pose identities and actual piece symmetries for deduplication.
- Emit a closure certificate and bounded diagnostics. Exceeding a time, memory,
  or pose bound means unresolved feasibility, not permission to silently disable
  unrepresented moves.

A clear halfspace turn preserves separation throughout its path: the cutting
plane is invariant, and participants undergo one rigid rotation. The central
cell stays fixed because it is inside every cutting plane. Cosmetic mesh gaps
cannot be used to decide legality.

The guarded catalog represents a piece relative to the center of its last
driving grip. On a switch between grips, two aligned outside cut faces determine
a finite anchor frame. Subsequent rotations on the same grip are differences
between absolute center phases. The verified catalogs contain, including the
fixed core, 4,480 entries for A, 2,583 for B, 4,981 for C, and 7,471 for D.
These are guarded entries across pieces, not whole-puzzle state counts. Preserve
the phase and cut-contact invariants validated over geometrically compatible
source configurations; a random-walk sample cannot replace the closure evidence.

Distinguish complete aligned turns from intermediate motion. Arbitrary partial
rotations are continuous even on a normal cube. Some handoffs require both
opposite caps to turn before a third full grip becomes free. With the current
local middle-alignment rule these are two commuting single turns; their
intermediate state is retained instead of requiring an atomic paired request.

Completion: production generation reproduces the verified catalogs, stable IDs,
and certificates for the agreed cut-alignment rule. Repeat the feasibility and
closure gates for new parameters; a continuous or infinite logical state must
not be represented by silently truncating a finite catalog.

## 3. Compile into existing abstract and geometric formats

Add `scripts/melty_packages.py` to export the verified finite data:

- Relational source, bindings, catalogs, source-phase guards, blocker predicates,
  and deterministic completion profiles. Each request must select precisely the
  pieces in the active cap. Keep stationary choices explicit where required.
- Pair incompatibilities or equivalent occupancy invariants for incompatible
  poses. Factor the conflict graph into shared sets or verified cliques rather
  than one predicate per conflicting pair: the unpruned D research graph has
  over four million edges. Verify that the compact encoding covers exactly the
  original conflicts and stays within compiler limits. Encode center phases and
  piece-to-center phase guards abstractly.
- Single-cap requests with source/target center phases and local middle
  alignment guards. Split older paired witness steps in both orders and verify
  that their endpoints agree. Keep one signed rotation track per single turn.
- Explicit directed operations and inverses, including phase guards and event
  witnesses. Preserve participation when an oriented/marked center's abstract
  endpoint is a self-loop.
- `polyhedral-euclidean` prototypes, `modelByDomain`, proper `placementFrames`,
  `operationTracks`, and the real `angularStops`. Check existing schema limits;
  do not assume all pieces are congruent or that stops are quarter turns.
- A `polyhedral-port-diagram` realization when it can express the same port
  bindings clearly. Defer spherical presentation until the solid model is sound.

Use the immutable `GeometryDefinition`, frames, and seekable animation API
described in [kernel-api.md](kernel-api.md). The geometric interpreter evaluates
the authored paths. Renderers consume assets and poses; controllers submit
revision-checked requests. No Three.js objects, collision callbacks, DOM access,
or animation scheduling belongs in the abstract core.

The expected implementation requires authoring data and package integration,
not a Melty-specific core opcode. Change public contracts only if a demonstrated
gap remains after the finite model is established, with native/WASM declarations,
consumer checks, documentation, and appropriate independent version changes.

Completion: Melty A–D load and execute through the existing public native and
WASM APIs without application-specific rule evaluation.

## 4. Integrate prebuilt packages and controls

Artifacts live at `packages/melty-a/`, `melty-b`, `melty-c`, and `melty-d`, using
the repository's source, bindings, package, realization, and pin conventions.

- Add each generator, its transitive inputs, and runtime artifacts to
  `scripts/puzzle-inventory.json`. Generate via `npm run build:puzzles`; preserve
  deliberate fixture updates and deterministic `packages/build-manifest.json`.
- Keep `npm run build`, SDK packaging, and Pages publishing on prebuilt data.
  Freshness checks precede WASM compilation; compatibility checks follow it.
  No native authoring or Python generation is added to the publishing path.
- Add puzzle selection and grip/destination controls in the reference app and
  standalone canvas example. Reuse the existing non-quarter-turn interaction
  pattern; labels must identify actual alignment destinations and direction.
- Show blocked choices through existing diagnostics. Preserve revision checks,
  undo, redo, session serialization, and replay of the selected path.
- Explain nominal grip names and shape changes. Avoid labeling every move a
  quarter turn or treating a visibly similar endpoint as the same event.

Completion: ordinary app loading and independent consumers use identical
prebuilt data and kernel semantics. No deployment trigger or Dockerfile change
is needed.

## 5. Verification and release order

1. Check the regular-axis control case against ordinary 3×3 geometry and rules
   under consistent center marking and orientation conventions. Assert the
   baseline 27-cell/26-exterior-piece topology and volume conservation.
2. Check A's initial 7/5 candidate counts, candidate angles, and whole-solid
   handoffs. Check all four ray symmetry groups and D's absence of inversion.
3. Independently verify certified clipping, every exported transition, inverse,
   blockers, the stationary core, and incompatible-pose exclusions. Compare rejected
   ordinary 90° handoffs with valid alignment endpoints.
4. Establish complete finite closure and preserve a certificate. Add reference
   sequences with source provenance only when their declared cuts and geometry
   match the modeled puzzle.
5. Validate all schemas, source/catalog/package agreement, semantic/fixture pins,
   and construction of every realization. Check repeated generation is identical
   and read-only checks leave tracked files unchanged.
6. Run native/WASM parity, independent SDK consumers, session replay, geometry
   endpoints/intermediate frames, and browser interaction/smoke checks under the
   repository subpath. Keep heavy work sequential, compilation at one or two
   jobs within 4 GiB, and browser checks at one worker with display variables unset.

A was implemented first because its initial position counts have an independent
published check; D exercises chirality and a different stop structure. All four
now ship together. General six-ray authoring and alternative cut depths remain
future work, with their own topology and closure validation.
