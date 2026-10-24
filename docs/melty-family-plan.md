# Melty family implementation plan

Status: Melty A–D are implemented for the declared 85°, d=1/3 cube models.
`scripts/melty_model.py` reconstructs the analytic rays and reviewed placement
recipes; `scripts/melty_packages.py` generates the prebuilt package and both
rigid realizations. The checked-in authoring inputs live in `scripts/data/`;
generation and runtime have no dependency on the ignored research directory.
Each package has 27 piece-specific domains, including the fixed internal core.

| Family | Catalog placements | Witnessed placements | Witness nodes | Directed requests |
| --- | ---: | ---: | ---: | ---: |
| A | 1,466 | 1,466 | 6,818 | 492 |
| B | 1,208 | 1,208 | 766 | 202 |
| C | 1,692 | 1,682 | 10,587 | 480 |
| D | 2,020 | 2,020 | 1,667 | 978 |

C retains ten unclassified memberships, five each on RBD and FLD. They are
marked in the authoring seed and listed in the package manifest; absence of a
witness is not treated as proof of unreachability. Every witnessed placement
replays through the compiled relational program. Pruning is numerical evidence,
not a formal completeness proof. Sharing cut frames across physical pieces
remains a storage refinement; complete colored bodies retain their own domains.

All four are available in the reference app and independent Canvas SDK example.
Principal turns are about 85.401744°, except C's D grip at about 89.709219°.
Additional buttons carry signed angle tooltips, and paired buttons turn
opposite caps simultaneously. Native tests replay each complete witness graph,
including paired-only handoffs and blocked individual caps.
Shared rotation tracks reduce duplicated geometry data without adding an
abstract opcode or changing public method/buffer versions.

Known limitation: requests currently require an intersecting cut to be free at
the endpoint. This completed-handoff restriction is stronger than the geometric
cut-alignment rule. For example, Melty D accepts `U D_e` (F remains free) but
rejects `U D_e_inv` (only opposite cuts U and D remain free), even though the
latter has a clear driving cut, catalogued destinations, and a valid assembly.
Supporting these intermediate discrete states remains follow-up work; the
current packages retain the handoff restriction.

B–D use the existing articulated binding format to share local movements only
when their entire transport and signed rotation path agree. Scoped events and
reciprocal local inverse bindings preserve the animation and exact witnesses.
Binary solid exclusions use cardinality bounds, and repeated memberships use
predicate aliases, within the existing language budgets. A's original package
and semantic pin are retained. Authoring generation and native validation run
in separate processes so their graphs do not overlap in memory. The supplied
WASM heap starts at 32 MiB and can grow to 384 MiB for the larger D catalog.

The broader counts below describe the original guarded supersets used in the
investigation, before the production catalog pruning summarized above. The
following design and gates record the implementation approach. Remaining
investigation includes classifying C's ten memberships, independent comparisons
with physical puzzles, and extending the declared parameters.

Implement the offline authoring model first, then ship prebuilt puzzle packages
through the existing portable kernel. The essential feasibility gate is a
complete finite catalog of piece placements and center phases. That gate now
has computer-assisted closure verification for A–D, including paired opposite
turns, exact rotation identities, and conservative geometric predicate audits.

The agreed rule is geometric: a twist is available when its cutting plane
passes between complete pieces. Internal mechanisms impose no additional
restrictions. Later investigation establishes a complete method for discovering
next handoff angles and provides complete guarded catalogs for the declared
model. These are supersets of reachable handoff placements, not claims that
every entry or combination is reachable. Continuous intermediate turn angles
are outside the finite logical catalog.

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
opposite caps to turn before a third grip becomes free. The closure argument
combines commuting turns of those disjoint caps; production move generation and
controls must retain these paired handoffs.

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
- Explicit paired opposite-cap requests, with disjoint participant sets, both
  source/target center phases, and the corresponding two geometric tracks.
  Verify that existing operation-track formats express these paths before
  considering any public API change.
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
