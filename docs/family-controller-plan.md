# Twist-family controller and generated motion plan

Current Melty rule revision: A–D now use six independent single-cap families.
A destination needs alignment with the stationary middle, not an immediately
usable intersecting full cut. Opposite-cap turns commute and old paired steps
split into two legal history entries. The paired Melty requirements and counts
below describe the earlier checkpoint and are superseded by
[the current Melty rules](melty-family-plan.md). Generic multi-driver controller
support remains available for other authored packages.


Status: Helicopter/Melty checkpoint implemented and ready for browser review. Later
Cube/Bandage, Bagua and Gear stages remain planned.
Baseline: core commit `a347002`, kernel 0.5.0 / API 5.

Implementation uses `ControlCatalog` for the immutable controller definition and
`Controller` for resolution. The optional `Twisty::controller` library and matching
WASM/TypeScript APIs are versioned as kernel 0.6.0 / API 6. Existing transports,
predicate semantics, session schema and geometry buffers keep their versions.

Current work includes six-stop Helicopter phase refinement, Melty single/paired
families, lightweight session results, generated rotations, and shared transport
selectors for Melty paths. These selectors replace the planned per-placement
path expansion; each Melty realization has one common operation table shared by
all piece domains. Section 13 records the initial delivery and measurements.

## 1. Result and scope

Replace the main wall of movement buttons with two steps: select a twist family,
then select its destination angular stop. Show the current stop on a circle and
explain blocked destinations using the existing permission diagnostics. All
rendered views and alternative frontends consume the same portable APIs.

Implement Helicopter first, followed by Melty A–D, then Cube/Bandage, Bagua, and
Gear variants. Helicopter and Melty provide the first complete browser review
checkpoint. Keep notation, keyboard shortcuts, piece-destination selection, and
an advanced movement list available throughout the transition. Packages without
control metadata continue to use their existing controls, including flat models.

The first release selects among authored, certified paths. It does not invent
extra revolutions or assume that a shorter rotation is legal. Additional path
choices can be added later through the same contract. Preserving current motion
directions and Gear cog winding takes priority over choosing a shorter path.

Acceptance includes portable control queries, state-backed phases, atomic paired
Melty moves, generated interpreter motion, and a usable two-step browser control.
General textual language syntax, algorithm conjugation, continuous drag-to-twist,
new meshes, and changes to physical puzzle rules are separate work.

## 2. Boundaries and current implementation

| Layer | Responsibility |
| --- | --- |
| Abstract core | Placements, finite transports, predicates, simultaneous execution, witnesses |
| Authoring/compiler | Declare families, phase readers, routes, reference bindings and completion profiles |
| Portable controller | Read the state, resolve a destination intent, query permission, submit a revision-checked request |
| Geometric interpreter | Angular coordinates, rotation generators, path evaluation and endpoint certification |
| React/Three.js application | Family selection, circular controls, input, scheduling, camera and drawing |

The compact core `TwistFamily` is a storage object. One visible family can span
multiple domains, reference charts, explicit maps and transport unions. Give the
controller its own stable authored family IDs; never discover its controls by
examining which compression representation the optimizer retained.

Current state remains a piece-to-placement vector. There is no universal face
angle field. `Session::snapshot()` currently computes legal and candidate requests
for the entire puzzle; the new UI must not require that full query on each click.
Current geometric tracks already distinguish world and local rotations and share
path libraries. Extend that machinery rather than moving animation mathematics
into React or expanding compact transports back into dense tables.

## 3. Angular position is part of the abstract state

A driver has finite stop IDs and an authored reader from existing placements to
those IDs. Numeric angles belong to the realization. The reader is a bounded
lookup, usually on a center or mechanism piece, not a search through history.
It must work after moves, algorithms, scramble, undo, redo and saved-state import.

If the current placements identify only a rotational equivalence class, refine
the relevant placement catalog to distinguish the required finite phases. This
changes those puzzle definitions and their pins, but requires no new core state
field, domain semantics, or geometric data in the abstract core. Do not use a
browser-only angle counter, add physical pieces, or enumerate unrestricted twist
closure to recover missing phases.

| Puzzle | Current information | Planned treatment |
| --- | --- | --- |
| Helicopter | Three mechanism classes per grip, modulo a half turn; six geometric stops | Lift each mechanism's three placements to six marked stops |
| Melty A–D | Center placements already encode each declared phase | Reuse those placements and exact authored phase tables |
| Cube and Bandage | Six unoriented center placements | Use the 24 oriented center placements, four at each face |
| Bagua | Center catalog already contains eight phases per face | Verify and expose the existing placement-to-stop mapping |
| Gear, Extreme and custom | Centers are unoriented; cog phase is already separate | Refine centers to four phases per face; retain all existing edge/cog state |

For each refinement, define a projection from new placements to the old ones.
Occupancy, collision resources, guards and physical solvedness use that projection
where orientation was previously ignored. For example, the two marked Helicopter
stops corresponding to an old home mechanism class both satisfy its old goal.
The new bookkeeping must not turn an unmarked center into a supercube constraint.

Retain piece identities and old placement names where possible; append new names.
Global numeric indices remain definition-specific. Recompute applicable finite
actions for the refined catalog, using the exact authoring frames to establish
their laws. Do not infer an action from an expected orbit size alone.

### Helicopter refinement

Use stops `a` through `f` at
`0, alpha, pi-alpha, pi, pi+alpha, 2*pi-alpha`, where `alpha = acos(1/3)`.
Their projections are `(a,d)`, `(b,e)`, `(c,f)`. There are twelve grip drivers.
This adds 36 mechanism placements and no pieces; exterior placements stay intact.

Preserve the existing request IDs and exterior transports. A legacy request
whose canonical source is `p` applies to lifted source `p` or `p+3`; its mechanism
target is correspondingly `q` or `q+3`, modulo six. Lift its phase predicate to the
two matching source placements. Build these rows as compact reference differences
or small exact maps according to the existing storage cost rules.

The destination resolver uses the actual six-stop source. At source `p+3`, a
desired destination `q` selects the legacy route whose canonical destination is
`q+3` modulo six. Check this indexing against independent expansion rather than
duplicating request-name parsing in the browser.

Retain the interpreter's current signed principal-angle convention, including its
fixed half-turn tie direction. A full-circle stop label is a finite orientation,
not an accumulated revolution counter. A logical inverse need not retrace the
same geometric path, especially for a half-turn tie; distinguish these contracts.

### Invalid or unavailable readers

Validate that a reader is single-valued on its declared placements. If a valid
imported state puts its frame piece outside that reader's coverage, return
`Unavailable` for that control with a diagnostic. Never guess stop zero or reject
an otherwise valid abstract state merely because it lacks a usable control frame.

## 4. Authored control catalog

Add optional `controls`, with its own `version: 1`, to the execution binding
catalog. Compile it once into immutable indices shared by sessions. It contains:

- Stable family and driver IDs, labels, display ordering, and single/paired kind.
- Ordered finite stop IDs for each driver and placement-based phase readers.
- Routes indexed by family and source-stop tuple, leading to target-stop tuples.
- For each route, a concrete request, complete choices or a named existing
  completion profile, a stable path ID, inverse route, and optional preference.
- References to the core reference families/bindings where useful for inspection;
  these are optional provenance, not the source of the controller's identity.

Do not store axes, radians, pivots, easing, camera information or mesh IDs here.
A path ID distinguishes authored routes even when their endpoint permutations
agree. The geometric realization supplies its angular lift and generator data.

Reuse completion profiles and parameterized route templates. For example, one
Melty angular-difference request can serve several source/target pairs. Avoid
copying an entire permission relation or transport for each displayed destination.
The compiler checks symbols, reader conflicts, source/target arity, inverse links,
and route/profile compatibility. The planner checks the actual target phases and
permission at runtime; metadata is never a certificate of legality by itself.

Catalogs without `controls` keep their original canonical serialization. Optional
metadata in a new package is covered by that package's definition fingerprint.

## 5. Destination resolution and public APIs

Add a portable optional `puzzle_controller` library, outside the minimal core.
Its immutable `ControllerDefinition` shares the compiled `Execution`; queries take
a `Session` or explicit state. A TypeScript `KernelController` retains its session
and wraps matching WASM methods. It performs no separate JavaScript rule solving.

Proposed API shape; JSON methods mirror the native typed interfaces:

```ts
type TurnIntent = {
  family: string;
  targets: Record<string, string>; // driver ID -> destination stop ID
  pathId?: string;
};

controller.catalog();
controller.familyState(family);              // current stops + revision
controller.destinations(family, options);    // scoped, paginated previews
controller.planTo(intent, expectedRevision); // no mutation
controller.turnTo(intent, expectedRevision); // one atomic commit
```

The source stops always come from the supplied state or live session. Callers do
not get to assert a stale source phase. Both preview and execution return the
definition digest, revision and enough route identity to explain the result.
`turnTo` resolves and plans again against its checked revision; it never accepts a
client-supplied witness as trusted execution data.

Resolution proceeds as follows:

1. Check the definition, family, target IDs, path ID and expected revision.
2. Decode the source-stop tuple from the authoritative state.
3. Look up routes to the requested target tuple and bind their declared profiles.
4. Ask `Execution::plan` to validate each relevant complete tuple against
   `R(s,u,a_1,...,a_n)`, target invariants and simultaneous-writer rules.
5. Verify that successful target states decode to the requested stops.
6. Return the declared preferred valid route, a choice request, or diagnostics.
   Commit only through the existing session history machinery.

Results distinguish `Planned`, `Committed`, `Blocked`, `NoDirectRoute`, `NoOp`,
`ChoiceRequired`, `Unavailable`, `StaleRevision` and invalid input. A blocked
preview retains existing implicated-piece/evidence diagnostics. `NoDirectRoute`
means that no authored atomic route exists; it is not a predicate rejection.

When several routes reach the same target, the catalog may nominate a preferred
path. Otherwise return `ChoiceRequired`, especially when other pieces' endpoints
differ. Do not choose the first solver result. Angular distance can inform an
authoring preference or a UI suggestion, but the abstract resolver does not
calculate it from geometric coordinates. Selecting the current stop is `NoOp`
unless the user explicitly selects a declared nontrivial loop route.

The controller does not execute a raw reference difference as a move. It resolves
to a declared request and complete tuple; the permission relation remains the
authority even if the requested reference transport is mathematically defined.

## 6. Pairing, coupling and winding

### Melty paired caps

Represent an opposite-cap family with two drivers and linked circles. The user
selects a target for each cap and presses one Apply button. Submit the resulting
tuple as one existing paired request; never execute its two components in series.
Some paired moves are legal even though both corresponding single moves fail.

Offer single-cap and paired controls separately. In paired mode, show availability
for the joint target selection, and optionally the projection of legal pairs onto
each ring. A failed single-cap query must not disable a node needed by a legal
pair. Keep draft target selection separate from committed current-stop markers.

### Gear coupling

Read both face and cog phases from actual state. For a coupled quarter turn, the
authored convention turns the two opposite faces and keeps the middle carriers
fixed; cog rotation follows its existing signed `5*pi/3` path. Ordinary turns
remain ordinary when the relevant middle layer has no geared edges. The existing
relation selects the allowed mode; the controller does not reproduce that rule.

Opposite-face labels do not determine a rotation sign. Preserve the canonical
coupling convention, including identical physical results for the applicable
opposite requests and the established UF/UR direction cases. Show driven opposite
faces as a side effect of the selected route rather than another independent
destination choice.

A face destination does not uniquely identify Gear motion. A face rotation of
`+pi/2` and `-3*pi/2` has the same face endpoint, but its geared edges rotate
`+5*pi/3` and `-5*pi`, respectively, and can have different abstract cog endpoints.
A full face revolution can also change cog state. Path/winding alternatives must
therefore resolve to actual validated tuples and events, not an animation-only
override. The first release exposes existing authored paths; it adds no arbitrary
full-revolution variants.

For Cube and Gear's later rollout, add any missing direct half-turn routes as
explicit authored atomic operations, with inverse choices and exact component
transports. Compare them to the appropriate two-quarter-turn endpoint as an
oracle, but validate the new atomic route in its own right. A destination click
must not silently become two history entries or pass through an intermediate
state whose legality was assumed. Unauthored destinations remain `NoDirectRoute`
until this work is complete.

## 7. Rotation generators in the interpreter

Add optional versioned `motionFamilies` and `controlLayout` declarations to
realizations. The former describes paths; the latter maps stop IDs to display
angles and a consistent ring orientation. Both are interpreter data. Ring layout
may be nonuniform, as for Helicopter and Melty.

For a fixed-axis rigid reference, share a unit rotation generator `Xi`, consisting
of a normalized axis, pivot, and an explicit world/local frame. Store each stop's
scalar angular coordinate. For a selected authored lift `k`, use

```text
Delta = theta_target - theta_source + 2*pi*k
motion(t) = exp(t * Delta * Xi)
```

Apply this relative motion to the actual source pose: world rotations multiply
on the left and local rotations on the right. The frontend supplies eased progress
if desired; the interpreter's seekable evaluator stays deterministic. Do not ease
twice or reconstruct a source pose from the driver's angle alone.

Giving each stop `A_theta = theta * Xi` makes subtraction valid because these
generators commute. Different axes generally do not commute: subtracting their
matrices or logarithms is not a valid general animation rule. Use ordered
generator products or the existing explicit shared paths for those cases.

Allow per-reference or per-component generator/rate bindings within a family.
This handles fixed carriers, rotating faces and driven cogs without requiring
every moved piece to share one rotation. Gear cog motion starts from its own
current phase and preserves the full signed rate; never replace 300 degrees with
the equivalent endpoint angle of -60 degrees.

For symmetry-related instances, derive world generators by conjugating with the
realized symmetry frame. A finite abstract group element alone supplies no axis
or pivot. Keep those geometric frames separate from the core's finite tables.

Events remain stable path certificates. A thin event binding identifies the
motion family, relevant reference/component binding and authored lift/path ID;
the transition's source/target placements supply the state-dependent phase.
Derive the driver phases from the recorded transition, not the current live
session, because replay and queued animations may be several moves behind it.

The current articulated event adapter certifies through a representative
request's default profile. Keep that restriction for migrated existing paths.
If later path variants need distinct completions under one request, extend the
certificate to identify the exact profile rather than silently reusing the
default profile's event.

### Preparation, sharing and validation

- Resolve generators when preparing one transition and share immutable family
  descriptors. Retain only the small active animation data required for sampling.
- Read participants and targets through shared transport lookups. Do not create
  permanent request-by-placement path arrays or expanded reference differences.
- Certify generated endpoints against exact abstract transports, geometric frames,
  labeled ports and declared body symmetries. Check full signed path samples
  separately; an endpoint comparison cannot detect a lost revolution.
- Preserve participating self-loops and any nonzero motion they carry. A piece
  can participate even when its final placement or visible shape is unchanged.
- Keep explicit path support for unsupported generator patterns and report those
  fallbacks per family. Existing limits on step count and numeric bounds remain.
- Use the same motion definition for cubic, spherical and diagram views when
  their realization supports it. Planar models keep their existing paths for now.

## 8. Browser behavior and command lifecycle

Render a compact family selector followed by a circular stop control using
HTML/SVG. Draw stops at their authored display angles; identify them with stable
stop labels, angle tooltips, and a prominent current marker. Use shape and text
as well as color to distinguish current, chosen, legal and blocked states.

The circle is expressed in the family's authored local frame. Camera movement
does not change its coordinate system. A small direction indicator and family
highlight in the views relate the ring to the puzzle; no camera-dependent sign
inference is needed. A selected destination previews the actual authored path,
including its direction and any driven face/cog side effects.

Arrow keys move focus among stops without executing. Enter/Space activates a
single-driver destination; paired mode uses an explicit Apply button. Provide
accessible labels and a textual destination list for small screens or users who
cannot use the diagram. A blocked selection can reveal the current explanation.

Queue destination intents, not operation IDs resolved from old phases. When an
intent reaches the front of the application queue, resolve it against that live
session's current revision. Pin each queued intent to its definition and session
generation so puzzle replacement or reset cannot send it to a different session.

Explicit preview-then-confirm operations carry their preview revision; return
`StaleRevision` and refresh rather than silently changing the approved route.
This differs from an ordinary queued destination click, which intentionally means
"go to this stop when this command runs." Keep the distinction in the API and UI.

Update current markers from committed state. While a transition is being drawn,
keep its animation progress indicator separate. Serialize move execution and
animation through the existing queue; no UI angle counter becomes authoritative.
Undo, redo, notation, keyboard and piece-destination commands use the same queue
and refresh the active family's phases. Cancel pending work on session changes.

Keep exact executed requests, choices and events in schema-2 history. Family
intents may be optional presentation metadata, but replay always uses the recorded
completion. Redo preserves its route. Undo preserves the established logical
inverse/event convention; reverse-path playback is a separate presentation option
and must not fabricate an unvalidated inverse event.

## 9. Performance and the 4 GiB budget

Add a lightweight session snapshot and command-result mode containing state,
digests, revision, goal and history summary, without global legal/candidate lists.
Existing full snapshots remain supported. The new controller and UI use the
lightweight mode for commits, undo/redo and current-phase refresh; otherwise the
old full snapshot computation would erase the benefit of scoped queries.

Index routes by family and source-stop tuple at construction. A family-state
query reads only its phase readers. A destination preview plans only relevant
routes; do not enumerate every request or all reachable puzzle states. Query
piece-destination overlays separately when a piece is actually selected.

Return the current marker immediately. Populate availability progressively with
bounded preview batches and yield between them in the browser. Paginate paired
target queries and distinguish unqueried nodes from blocked nodes. Exhausting a
query budget must never produce a false legality result. Discard results whose
revision or session generation no longer matches.

Share catalogs, route templates, generators and compiled executions across
sessions and views. Cache scoped results for the current revision with a bounded
entry count; do not retain a cache for every historical state. Free stale preview
witnesses and disposed WASM wrappers. Do not add meshes or triangles for controls.

Measure abstract compilation, interpreter compilation, first family display,
one destination preview, paired preview, command planning, serialized package
size and retained transport/path memory before and after. Report native and WASM
measurements separately. Initial paint must not wait for a full-puzzle legality
pass; no unbounded table expansion or new memory-limit increase is acceptable.

## 10. Files, versions and compatibility

| Area | Planned files/work |
| --- | --- |
| Portable controller | `include/twisty/controller.hpp`, `src/controller/`, optional CMake target |
| Execution/session | Shared control metadata, scoped planning, lightweight results in existing public interfaces |
| Authoring | `src/authoring/`, Helicopter/Melty/Bagua exporters, Cube/Bandage/Gear factories |
| Interpreter | Family descriptors and generated sampling under `src/interpreter/` |
| WASM/SDK | Bindings, generated declarations, `kernel/index.ts`, independent consumers |
| Schemas/packages | Control catalog and realization declarations, regenerated packages and pins |
| Reference UI | New components under `web/src/controls/`, integration in `App.tsx` |
| Documentation/tests | Kernel API contract, controller examples, focused native/unit/browser coverage |

Release the public addition as kernel 0.6.0 / API 6 with
`controllerAPIVersion: 1` and explicit control/generator capabilities. Keep
definition/source/package format 2, predicate version, session schema 2, geometry
API 1, and existing pose/frame/mesh buffer layouts. The new geometry declarations
are gated by an advertised capability and their own version; the sampling API
and binary buffers do not need an incompatible change.

Catalog version 1 remains readable; its optional `controls.version` is validated
when present. Extend realization schema 1 with capability-gated declarations and
reject unknown required capabilities. Do not silently ignore a motion family in
a runtime that cannot evaluate it. Keep old explicit realizations supported.

Phase-refined definitions and catalogs with controls receive new fingerprints.
Regenerate matching realization pins, manifests and fixtures. Format-1/format-2
packages without the additions retain their canonical fingerprints and old saves
remain loadable with their matching definitions. No automatic orientation guess
or save conversion is included. Expose advanced controls when an old definition
has no family catalog.

## 11. Implementation order and review gates

1. **Freeze the baseline and specify fixtures.** Save representative phase,
   transport, legality, event and sampled-motion observations from the committed
   packages, including Helicopter half-turn ties, paired-only Melty moves and
   Gear direction cases. Define tiny hand-checkable control catalogs.
2. **Build portable controller plumbing.** Implement control metadata validation,
   phase readers, route indices, result statuses, revision checks and lightweight
   session results. Exercise them through native and independent WASM consumers
   before using the browser as the only client.
3. **Migrate Helicopter.** Lift its mechanism phases, prove projection parity,
   author twelve control families and introduce generated rotation paths. Keep
   existing request/inverse identities. Test all six sources and destinations.
4. **Deliver the first UI.** Add family selection, ring, path preview, blocked
   diagnostics and intent queueing. Verify all Helicopter views, keyboard access,
   undo/redo/import, and rapid input across phase changes.
5. **Add Melty A–D.** Reuse exact center phases, add single and linked paired
   controls, migrate compatible tracks to generators and replay existing witness
   graphs. Browser review checkpoint: both puzzles have usable two-step controls
   and no requirement to navigate the advanced movement list.
6. **Extend Cube/Bandage and Bagua.** Refine Cube center phases with projected
   goals/resources, add certified atomic half turns where needed, and expose
   Bagua's existing stop catalog. Verify elongated bandage visuals and blockers.
7. **Extend Gear variants.** Refine center phases, preserve ordinary/coupled
   completion selection, add certified half-turn routes, and migrate component
   motion with explicit signed cog rates. Test normal, Extreme, plain and custom
   masks, particularly UF-only and UF+UR.
8. **Finish packaging and review.** Regenerate all affected artifacts, publish
   the native/WASM contract and examples, record storage/timing results, and run
   the focused integration suites. Keep the advanced path available for packages
   without control metadata. Prepare changes for browser review before committing
   controller work unless the user requests a commit earlier.

The first five stages are the initial usable delivery; the remaining stages
complete coverage of the current three-dimensional presets. A phase-refinement
or motion fallback that changes physical behavior blocks that puzzle's rollout
until it is resolved; other completed families can still be reviewed.

## 12. Verification and completion criteria

Use small exhaustive tests for the new finite controller machinery and targeted
regression cases for real packages. Reuse the existing transport and witness
oracles rather than multiplying identical checks for every UI presentation.

- Readers decode every declared stop, report uncovered states, and refresh after
  every session edit. No phase lives only in the frontend.
- For each refined puzzle, projection commutes with every legacy transport;
  old physical legality, participants, goals, events and labeled endpoints agree.
  Independently enumerate phase refinements of affected support for small cases.
- Every declared route reaches its advertised target tuple. Missing routes,
  rejected tuples and ambiguous paths produce distinct, stable outcomes.
- Stale preview/commit, failed moves, no-ops and malformed intents leave state,
  revision and history unchanged. One destination action makes one atomic entry.
- A recorded Melty paired-only move is selectable and executes without either
  single-cap move being required first. Replay the A–D witness graphs.
- Generated and old paths agree at endpoints and selected interior samples,
  including Helicopter half-turn signs and Gear's full signed cog rotations.
  Test world/local order, symmetry-derived axes and noncommuting fallback paths.
- Undo/redo and save/reload restore exact refined state and events. Legacy pinned
  saves remain readable. Geometrically identical stops are not accidentally
  merged when their marked phases or cog effects differ.
- An independent native consumer and copied WASM SDK consumer can discover a
  family, read its stops, preview and execute a destination, and sample motion
  without React, Three.js or DOM access.
- Browser checks cover family changes, blocked explanations, paired drafts,
  accessible keyboard flow, rapid queued targets, reset/import cancellation and
  synchronized cubic/spherical/diagram views. Obtain the user's visual review.
- Compare loading, scoped query cost and retained memory to the baseline. Derived
  transports and generator paths remain shared; the controller adds no mesh cost.

Run heavy checks sequentially, with one build job and one browser worker, after
checking cgroup memory. Run browser checks headlessly with `DISPLAY`,
`WAYLAND_DISPLAY` and `PWDEBUG` unset. Leave Dockerfile untouched. No push or
deployment is part of this plan.

## 13. Initial delivery: Helicopter and Melty

Stages 1–5 are implemented. The native/WASM controller, source catalog validation,
phase readers, revision checks, lightweight results, linked paired controls and
generated paths are available. The current marker is state-backed; no frontend
angle counter was introduced. The browser retains notation, keyboard shortcuts,
piece destinations and the advanced request list. Closely spaced stops use
radially staggered labels with connectors to their true angles, and a destination
list provides an alternative input.

Cube/Bandage, Bagua and Gear controls in stages 6–7 remain future work. Their
existing request controls continue to work. This checkpoint is uncommitted for
the user's browser review; no publishing or deployment was performed.

Helicopter has 44 pieces, 456 placements and the same 180 request/event identities.
Its marked mechanism action has three regular 24-element orbits. The authoring
compression retains the existing 36 extra reference charts, with no missing-
coverage fallback. Its storage optimizer retains 216 small explicit leaves;
these are cost choices, not missing reference coverage. Melty's placement and
transport catalogs are unchanged; only controller metadata and realization
storage change. Each variant exposes six single-cap and three paired families.

### Storage observations

Before refers to packages from `a347002`, interpreted by the current runtime.
Core payload is the existing normalized transport counter. Path backing counts
native pose-table track descriptors, their vector capacities, selector vectors,
and unique rotation libraries once. It excludes allocator overhead, resting
frames, meshes, authoring JSON and controller-index storage; it is not a total
resident-memory measurement. Package bytes include the new control declarations.

| Preset | Package MB, before → after | Core transport KiB, before → after | Path backing MiB, before → after |
| --- | ---: | ---: | ---: |
| Helicopter | 1.89 → 1.96 | 37.7 → 48.2 | 0.10 → 0.10 |
| Melty A | 5.05 → 5.20 | 512.0 → 512.0 | 4.02 → 1.84 |
| Melty B | 2.61 → 2.67 | 281.5 → 281.5 | 1.57 → 0.86 |
| Melty C | 5.27 → 5.42 | 584.6 → 584.6 | 4.37 → 1.82 |
| Melty D | 9.31 → 9.61 | 969.6 → 969.6 | 10.08 → 3.68 |

Melty D's retained dense path-index count drops from 1,975,560 to zero. It uses
49,896 small transport-region descriptors and the same 108 shared rotation steps.
Its Euclidean realization JSON shrinks from 6,446,138 to 1,745,185 bytes. All Melty
realizations have zero dense path indices after compilation; validation still
checks every participating endpoint. Core reference differences stay unexpanded.
There is no new mesh or triangle cost. Completion tuples are interned across
routes, and public controller discovery transfers descriptors rather than the
entire authored route catalog to every frontend instance.

Inspect storage with `build/native/controller_bench package.json realization.json`.
This is an internal native inspection tool, not an additional renderer API.

### Timing observations

These are single WASM observations in the current container, one runtime process
per before/after run. They exclude runtime startup and file I/O. Session load
includes compilation and definition transfer; geometry time includes construction
and the initial drawing. Full snapshot is the first full eligibility query at the
revision. These samples do not establish statistical performance differences.

| Preset | Session load ms, before → after | Geometry ms, before → after | Direct move plan ms, before → after | New phase query ms | New destination plan ms | Full snapshot ms, after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Helicopter | 240.9 → 244.5 | 21.0 → 22.6 | 3.44 → 1.68 | 0.368 | 2.43 | 19.3 |
| Melty A | 728.8 → 606.4 | 238.9 → 239.0 | 2.38 → 1.81 | 0.094 | 1.32 | 56.3 |
| Melty B | 269.3 → 292.7 | 96.6 → 92.1 | 1.30 → 1.36 | 0.042 | 1.20 | 28.5 |
| Melty C | 621.6 → 615.3 | 207.6 → 216.9 | 1.88 → 1.69 | 0.018 | 1.19 | 54.0 |
| Melty D | 1306.2 → 1307.6 | 350.0 → 330.5 | 2.43 → 2.27 | 0.026 | 1.60 | 94.6 |

Compilation cost remains similar. The main interactive gain is avoiding a full
eligibility scan for each phase read, destination preview and committed result.
Availability is populated one destination per browser task, with revision checks.
Reproduce with `node scripts/benchmark-controller.mjs`, and compare the baseline
with `node scripts/benchmark-controller.mjs --baseline a347002`, after packaging
the current SDK with `npm run package:kernel`.

### Verification

- Native controller checks cover rejected/no-op/stale requests, ambiguous paths,
  phase/inverse/replay behavior, malformed catalogs/generators, and old-vs-generated
  Helicopter path samples, including half-turn signs.
- Frozen transport comparisons pass, using the declared phase projection for
  Helicopter and exact comparisons for the other migrated presets. Format-1
  canonical fingerprints and replay remain covered by the frozen fixture.
- All four native Melty witness suites pass. Paired-only controller handoffs match
  the exact old request, choices, participants and events in one history entry.
- Native session/runtime/boundary and geometry checks pass. All 72 unit tests pass.
- Independent installed native and copied WASM consumers agree, including the new
  controller call and all existing preset/view combinations.
- WASM validation accepts 19 puzzles and 37 realizations. Full exporter
  reproducibility and fixture pins pass. Production build and type checks pass.
- Nineteen distinct headless browser checks cover the new controls, Helicopter and
  Melty regressions, Gear directions/custom masks, and reset. New control checks
  were repeated after the label-spacing fix.
- Heavy work was sequential with one build/browser worker within 4 GiB. No cgroup
  OOM events were recorded; Dockerfile is unchanged.
