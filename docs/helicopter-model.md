# Helicopter Cube: experimental exact model

The first jumbling milestone has a finite reference package and two C++ geometric realizations, displayed by Three.js. The C++ core executes its abstract placement relations; an offline Python authoring tool derives those relations with exact rational arithmetic. No coordinates, matrices, cut planes, tolerances, or meshes enter the runtime definition. Separate realization packages introduce geometry at the interpreter layer.

The package remains experimental pending independent human review of this specification and its fixtures. The rendered bodies use the model's ideal polyhedra with cosmetic clearances. Automated agreement with published results supports this stated model; it does not certify a manufactured mechanism, forced moves, or arbitrary intermediate-angle states.

## Scope and references

This models the straight-cut Helicopter Cube, using the six designated stops on each of twelve outward edge axes. It includes intermediate stops even when the only continuation is to turn the same axis again. Forced moves and arbitrary intermediate angles are excluded. It does not model the Curvy Copter's visible edge labels or its different cuts.

The reference stop convention and shape distributions are Matt Galla's [original analysis](https://twistypuzzles.com/forum/viewtopic.php?f=1&t=28265), independently reproduced by Jaap Scherphuis. [Jaap's analysis and solution](https://www.jaapsch.net/puzzles/helicopter.htm) supplies an additional center-exchange fixture and the ordinary four center orbits. Our construction and implementation were written for this repository; published factual counts and algorithms serve as external acceptance targets.

## Placements and state

| Piece family | Persistent pieces | Geometric locations | Full labeled placements |
| --- | ---: | ---: | ---: |
| Corners | 8 | 80 | 240 |
| Triangular face centers | 24 | 144 | 144 |
| Hidden edge mechanism pieces | 12 | 36, three per fixed axis | 36 |

A corner has three labeled local ports. Its three sticker orientations at each geometric location are distinct core placements. The published count of 80 locations ignores this distinction. A center has one local port, and a hidden edge has none. Each hidden edge has its own three-placement domain, so it cannot be assigned to a different axis.

As before, `State::placement_of` contains one placement index for each persistent piece. The hidden mechanism phases are represented by the twelve hidden pieces' placements. This package's separate `mechanism` record is empty; the core still supports exact mechanism variables for other models.

There are 260 abstract location resources and 2,424 exclusion resources. Each exclusion resource names a forbidden pair of geometric placements. Both placements occupy that same resource, so capacity-one occupancy rejects their coexistence. Corners with different sticker orientations at the same location share all resources. These resources are combinatorial conflict markers, rather than a universal subdivision of physical space. Unoccupied resources are allowed.

Imported assignments must satisfy domain and exclusion invariants. This does not certify their reachability. A saved history is checked by replay under the definition's SHA-256 digest.

## Stops and directed requests

Viewed from outside toward an edge axis, positive stops are clockwise. With `alpha = acos(1/3)`, the six stops are:

| Stop | Letter in operation ID | Angle |
| ---: | --- | --- |
| 0 | a | 0 |
| 1 | b | alpha |
| 2 | c | pi − alpha |
| 3 | d | pi |
| 4 | e | pi + alpha |
| 5 | f | 2 pi − alpha |

A hidden edge's geometry has half-turn symmetry. Its core phase is therefore the stop modulo three. The visible pieces still distinguish movements ending at stops separated by a half turn. There are fifteen directed requests per grip: three source phases and five destination stops, for 180 requests total.

`UF_ab` means rotate UF from phase zero to stop one. `UF_ad` is an ordinary half turn from phase zero. `UF_ba` reverses `UF_ab`. After a grip's absolute stop three, its source letter is again `a`; the request describes a relative rotation from that equivalent mechanism phase. This avoids storing an arbitrary winding history as puzzle state.

For a request with source phase `p` and destination stop `q`, the resulting phase is `q % 3`. Its inverse starts at that resulting phase and ends at `p + 3` when `q >= 3`, otherwise at `p`. This preserves the complete labeled state. Operation letters also avoid ambiguity with the notation parser's numeric repetition suffixes.

## Rule module

`finite-placement-relations@1` is a generic C++ backend. Each directed operation supplies a `pieceGuards` record and a `placementRules` entry for every domain:

```json
{
  "pieceGuards": {"mechanism/UF": "edge/UF/a"},
  "placementRules": {
    "some-domain": {
      "blocked": ["placement-that-prevents-this-move"],
      "transports": {"participating-source": "participating-target"}
    }
  }
}
```

A placement in `blocked` rejects the move. A placement in `transports` participates and moves to its declared target, even when the target equals the source. Every other placement stays stationary. An entry cannot be both blocked and participating. A piece guard checks the required source phase before any transport is applied.

Loading checks references, role consistency, inverse transports, inverse guards, and the initial assignment. Transport tables are partial maps because a move need not be defined on blocked placements. A permitted source maps to a permitted inverse participant; stationary placements remain stationary under the inverse. The core validates the resulting assignment before committing it, returning structured evidence for an ordinary blocked request and an invalid-definition diagnostic for a broken postcondition.

Existing cube and bandage packages continue to use `finite-footprint@1`, with exactly one occupant per declared cell and their original semantic digests.

## Exact offline construction

The authoring tool normalizes the solved cube to coordinates between −1 and 1. An edge cut has the form `n dot x = 1`, where `n` has two nonzero components, each ±1. A representative corner has vertices `(1,1,1)`, `(0,1,1)`, `(1,0,1)`, `(1,1,0)`, and `(1/2,1/2,1/2)`. A representative U-face triangle uses `(0,1,0)`, `(1,1,0)`, `(0,1,1)`, and the same inner vertex. The hidden-edge polyhedron and the symmetry action are explicit in the tool.

The jumbling rotation has a rational matrix even though its angle is irrational. For the axis `(1,1,0)`, the clockwise matrix has rows `(2,1,−2)/3`, `(1,2,2)/3`, and `(2,−2,1)/3`. Signed coordinate permutations generate the other axes and solved pieces. Two-move exploration discovers all 80 corner and 144 center shapes. Corner frames preserve all three labeled orientations.

For each candidate placement and axis, exact vertex bounds determine its role:

- If vertices occur strictly on both sides of the cut, the rigid piece blocks the move.
- If the whole piece is in the moving cap, it participates.
- Otherwise it stays stationary; boundary contact alone does not cause blocking.

A participating source whose requested rotation does not end in the finite catalog is also blocked. Exact matrix multiplication derives every supported transport. Separating-axis tests using integer face normals and edge cross products derive all positive-volume placement conflicts; touching boundaries are permitted.

The cut plane is invariant under rotation about its normal. Consequently, moving and stationary pieces remain on opposite sides throughout a legal rotation, and rigid rotation preserves separation among participants. This explains why these cut predicates check a path rather than only its endpoint. The six-stop restriction and hidden-edge rules specify which intermediate positions are admitted as logical states.

## Geometric realization

`helicopter-euclidean.json` and `helicopter-port-diagram.json` are separate packages pinned to the abstract definition's digest. They contain three shared polyhedral models, triangular port faces, a model assignment for each domain, and a row-major rotation matrix for each of the 420 labeled placements. Each of the 180 operations has an outward axis and source/destination stop indices into the clockwise angular-stop catalog. These floating-point values are visual data and do not affect logical equality or legality.

The C++ interpreter constructs convex-hull triangles and flat normals from the model vertices, shrinks bodies by 3% toward their centroids, and adds inset colored stickers to the declared port faces. The Euclidean scene uses seven shared mesh assets for 44 bodies and 48 ports. Three.js receives mesh buffers, material bindings, persistent piece/port identities, and sampled transforms. It does not derive placement geometry or decide which pieces move.

When loading either realization, C++ checks proper rotation frames, model/port coverage, declared model symmetries, and every participant transport in all 180 operations. A track must end at its exact target placement's geometric frame within `1e-6`, allowing only a declared model symmetry that preserves every labeled port. Port positions and normals must also agree individually. During animation, the interpreter validates the recorded core witness, rotates only its participants along the shorter angular path with smooth easing, and returns the canonical resting scene at the endpoint.

Hidden edges have no ports and have half-turn symmetry. A half turn can therefore leave a hidden edge's core placement unchanged while its mesh rotates continuously. Its endpoint frame may differ by the declared symmetry, but the physical mesh agrees. Corners retain all three labeled orientations and cannot use that quotient. Native checks compare mesh points immediately before the canonical endpoint to catch a discontinuity hidden by rebuilding the final frame.

The companion diagram shows the same 48 labeled ports in six fixed panels. C++ assigns each port to the panel whose normal is closest to its transformed face normal and projects its centroid into that panel. Port markers interpolate between source and destination positions; this is a schematic of identities and transport. The Euclidean view supplies the rigid-body motion. Both views share selection, core blocking evidence, and one authoritative session.

Choose **Helicopter Cube · jumbling** in the browser. The buttons show five destination choices for each grip's current source phase, including blocked requests so their evidence can be inspected. The default algorithm `UF_ab UL_af` demonstrates jumbling. Undo, inverse algorithms, seeded legal walks, and saved-session replay use the same core APIs as the cube presets.

## Verification and commands

```sh
npm run build:native
npm run check:packages
build/native/twisty validate --definition packages/helicopter/definition.json --json
build/native/twisty run --definition packages/helicopter/definition.json \
  --algorithm 'UF_ab UL_af' --policy transactional --json
build/native/helicopter_verify
npm run check
```

The C++ shape verifier projects corner sticker orientations away, while retaining all face locations and hidden phases. It reads the same compiled roles, guards, and transports as the core. Its breadth-first search is capped at 700,000 shapes and reproduces 654,117 oriented shapes, 28,055 classes under rotations, and 14,098 classes when mirror images are identified. Both complete published depth distributions agree, and maximum depth is 28. These are shape counts, not counts of labeled puzzle states. The verifier also checks placement exclusions for every reachable shape, that its projection is independent of corner labels, and that every symmetry preserves reachability and depth.

Native fixtures check participation, blocked source phases, face-piece blockers, inverses, undo/replay, and the center exchange `UF_ab DR_ab FR_ad DR_ba UF_ba`. That sequence returns to cube shape and moves exactly two centers between the four ordinary six-center orbits. Native/WASM parity checks cover its transitions, exact inverse, replay, and transactional rejection. Geometry checks cover all catalog transports at registration, solved port directions, intermediate frames, mesh continuity, canonical endpoints, stable hit bindings, and rejection of inconsistent axes or symmetries. Both realizations' mesh buffers and sampled transforms agree between native and WASM builds.

On 9 October 2026, the native/WASM production build and all three native checks, nine unit tests, and nine production-browser scenarios passed. Browser coverage includes realized package import, invalid-import recovery, jumbling, blocked-piece feedback in both views, inverse restoration, picking, and saved replay. Builds used at most two jobs and browser tests one worker; the container reported no memory-limit or out-of-memory events. Dockerfile was unchanged.

A subsequent performance pass replaced resource-name maps on the occupancy path with definition-local indices, cached snapshots by revision, and shared the session's immutable definition with both realizations. Already compiled definitions now validate once on session construction; the browser no longer precompiles valid sources separately. A local Node/WASM profile of six half-turn executions reduced average move processing from about 20.5 ms to 1.8 ms. Repeated snapshots dropped to about 0.05 ms; creating the two geometric interpreters took about 24 ms combined instead of 110 ms. These timings measure WASM work on the development machine, excluding browser rendering and configured animation duration. Regression checks preserve complete occupancy diagnostics, refreshed legality at new revisions, and geometric lifetimes after a session is disposed.

The generator is deterministic. `python3 scripts/helicopter_model.py` checks the definition, review data, and both realization packages without changing them. After reviewing a model change, regenerate with `--write`; review the resulting digest and deliberately update affected fixture pins. The review data contains abstract shape permutations and external acceptance counts, independently of the runtime definition. A visual-only change leaves the abstract semantic digest unchanged.

## Remaining review

Independent human review should examine the polyhedra, clockwise convention, admitted stops, source-phase quotient, conflict construction, and fixtures before promoting this experimental example to a reviewed reference model. The C++ geometric realization and its port/endpoint checks are implemented. Further physical fidelity would require a separate mechanism model and evidence about actual clearances and admitted moves.
