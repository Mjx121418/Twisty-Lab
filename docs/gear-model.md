# Experimental configurable Gear Cube

The **Gear Cube family** uses the supplied low-poly models, converted into the project's existing triangle-mesh JSON format. The normal model was created in a separate ChatGPT conversation; the complete Extreme model adds a plain edge. The original GLBs and their importers are not retained. This replaces the previous Boolean-construction investigation and viewer.

All three presets use the C++ `finite-placement-relations@2` backend, with generic exact placement-count guards. Euclidean and diagram views share a session. The gear checkboxes select which persistent edges have a cog; changing a checkbox creates a new solved definition and resets history. The configuration applies to piece identities, not fixed locations. Extreme, all plain, and custom configurations default to **Supplied model** and keep the **Edge model** selector for the earlier twelve-tooth or six-tooth prototypes; switching preserves state, selection, history, and cameras. The normal preset always uses the supplied six-tooth model and its corresponding phase diagram. Every model choice uses the supplied plain edge for unchecked pieces.

## Exact rules and configuration

A `gear-cube-prototypes` source has schema version 1, a semantic version, a puzzle ID, and a `gearedEdges` array containing any distinct subset of `UF UR UB UL DF DR DB DL FR FL BR BL`. The native/WASM compiler generates the same exact finite definition as the packaged authoring check. No coordinates, floating-point angles, meshes, or camera data enter the abstract definition.

There are 26 persistent pieces: eight corners, twelve edges, and six centers. A geared edge is one composite identity; its carrier and cog are visual components, not duplicate core occupants. Geared placements retain a carrier orientation and one of six exact cog phases. Geared and plain placements claim the same edge-location occupancy resources. Imported assignments cannot place a carrier assembly and a plain edge in the same slot. Featureless center orientation is quotiented, with unchanged participating centers retained in transition witnesses.

The **any-geared-middle-slice-v2** policy chooses coupling from current placements:

- `Fq` is legal when the middle layer contains no geared edge. It turns only the front layer 90° clockwise. Geared edges on that face ride with it without local spin.
- `F` is legal when at least one middle-layer edge is geared. It turns the front face 90° clockwise and the opposite face −90° about the same world axis. Middle-layer carriers, plain edges, and centers stay fixed; each geared middle edge advances one signed abstract cog phase. Its rotation angle belongs to the realization.
- Mixed geared/plain middle layers permit the coupled request. Exactly one forward request, and its inverse, is legal for every face in every valid configuration. Inverses use an apostrophe; numerical suffixes repeat primitives.

For a solved cube with only UF and UR geared, Uq/Dq are ordinary and R/L/F/B are coupled. After pieces move, legality is recomputed from their new locations. The coupled operation's `placementCountGuards` requires 1–4 geared middle edges. Ordinary operations block geared middle placements. Count guards and transport inverses are validated by the reusable core; controllers do not maintain a separate coupling state.

Face keys choose a legal geared or ordinary request at dispatch; explicit notation retains the distinction between `F` and `Fq`. These rules cover all geared masks explicitly. They do not establish that every mask corresponds to a manufactured mechanism. The model remains **experimental**, including the contact geometry and cog-spin convention. Shape-derived phase-dependent collisions and independently reviewed physical constraints are further verification gates; the existing capacity-one resource backend can represent additional placement exclusions without adding geometry to the runtime core.

[Jaap's Gear Cube analysis](https://www.jaapsch.net/puzzles/gearcube.htm) provides the relative outer/middle coupling and twelve-turn behavior. The current opposite-face convention is equivalent to the earlier 180°/90°/0° motion after removing a 90° whole-cube rotation. [Jim Storer's firsthand description](https://www.cs.brandeis.edu/~storer/JimPuzzles/ZPAGES/zzzGearCube.html) records the 60-degree cog increment. [Jaap's Extreme analysis](https://www.jaapsch.net/puzzles/gearcube2.htm) provides the eight-geared/four-plain reference and ordinary turns along the plain slice. These are factual references, not borrowed implementations. Published total state counts use additional visual and whole-puzzle quotients; the explicit carrier/phase state has not been equated with those totals.

Examples:

```text
Gear Cube:          R U F (R U F)'    R12
Gear Cube Extreme:  Uq R Fq (Uq R Fq)'
All plain:         Rq Uq Fq (Rq Uq Fq)'
```

## Articulated geometric interpreter

The reusable C++ interpreter adds `articulated-euclidean` and `articulated-port-diagram`. An authored mesh can be non-convex, allowing a toothed cog. Every rigid component has a resting frame for each placement and an ordered list of simple rotations for each participating operation/source placement. Rotations specify an axis, pivot, signed angle in radians, and `world` or `local` space. World rotations multiply on the left; local rotations multiply on the right. All steps use one shared eased progress value, so local cog rotation accompanies carrier motion continuously. Inverse tracks preserve direction and winding.

Registration checks rigid frames, mesh indices, complete domain/port bindings, participant-track coverage, and endpoint agreement for every declared transport. Declared label-preserving mesh symmetries allow featureless center orientations. The diagram uses explicit schematic translations between chart positions and local marker rotation to expose cog phase. It is not a mechanical motion model.

Three.js draws supplied assets and per-component matrices, using the same selection, picking, destination preview, and history controller as existing puzzles. It contains no gearing calculations. Custom renderers use the same portable API and buffer layout. See [the API contract](kernel-api.md).

## Supplied Gear Cube family models

The supplied model had five shared geometry prototypes: 8 corners, 6 centers, 12 edge bases, 12 six-tooth pinions, and 1 internal ball. The exterior has the requested 38 physical bodies. Each extra edge base and its pinion share one persistent abstract edge identity and occupancy resource, preserving the existing 26-piece exact definition and saved sessions.

The supplied triangles and colored surface regions are stored in [normal-meshes.json](../packages/gear/normal-meshes.json) without reshaping. Its 13 assets use the existing `vertices` and `triangles` representation, separating body and color patches. During conversion, a proper global rotation mapped `(x,y,z)` to `(x,z,-y)`, placing white at U and green at F; a uniform scale of 1.48 matches the reference application's cube size. The home assembly was compared against every source surface and color region before removing the binary. Source provenance, including its SHA-256, is recorded alongside the meshes.

The updated outer edge base from the later supplied complete Extreme model replaces `gear-edge/carrier` and its two `carrier-port-*` patches. Each base now has 174 plastic-body triangles and 14 triangles per colored patch. The normal cube and supplied Extreme/custom realizations share this revision; pinions, plain edges, other parts, and rotation tracks are unchanged. The mesh provenance records this update's source SHA-256 separately from the original model.

`scripts/gear_normal.py` combines these assets with the existing placement-frame and rotation-track authoring and emits [gear-normal-euclidean.json](../packages/gear/gear-normal-euclidean.json). Loading uses the ordinary public native/WASM geometry APIs and triangle buffers. There is no separate binary-model loading path. The interpreter computes its usual flat triangle normals, and face labels use the frontend's palette.

Each edge base and its colored patches follow the carrier frame. Each six-tooth pinion and its colored patches additionally spin 300° (5π/3) relative to that frame per coupled move, using the agreed 20:6 ratio through a 90° driving step. Both motions share the interpreter's eased clock; inverse tracks reverse the full signed travel. A local port can have separate patches on the base and pinion, keeping one color label and hit binding despite their different motion. The internal ball is a single frozen body component bound to `center/U` for picking; it has no separate abstract identity and does not rotate with the center. Its body-only `pieceIds` filter prevents duplicate copies.

The abstract catalog has six phases: `phase × 300°` modulo 360° gives six resting frames for a fixed carrier orientation. Six signed phase steps complete five full cog revolutions. Every realization pins its regenerated definition digest. Select **Animation → Inspect (3 s)** for direct browser review. The source explicitly sets `motion_clearance_verified: false`; neither the import nor endpoint checks certify continuous tooth contacts or physical clearances.

The supplied Extreme model has 8 corners, 6 centers, 8 edge bases, 8 six-tooth pinions, 4 `fixed_edge` bodies, and the internal ball. Its other 13 mesh assets match the normal model exactly. All surfaces and color regions in the reconstructed Extreme home assembly were compared with the complete source before removing the GLB.

The new plain piece is stored in [plain-edge-meshes.json](../packages/gear-extreme/plain-edge-meshes.json): one plastic body (162 vertices, 164 triangles) and two colored patches (12 vertices, 10 triangles each). Its canonical UF coordinates use the proper rotation `(x,y,z) -> (-z,y,x)` and the same 1.48 scale. Color patches are bound to the abstract edge's two local port labels. A plain edge has exactly these three rigid components; it has no separate carrier, pinion, bearing, or local phase spin. All three components share the same placement frame and layer-rotation tracks, so they move together through both ordinary quarter turns and applicable coupled quarter turns of the opposite faces.

`scripts/gear_normal.py` combines the shared normal parts with this plain-edge prototype to produce `gear-supplied-euclidean.json`, `gear-extreme-supplied-euclidean.json`, and `gear-plain-supplied-euclidean.json`. The reference frontend uses the supplied template for every custom mask, pinning it to the custom compiled digest. Which pieces use the plain domain is determined by persistent abstract identity, so a plain piece retains its mesh as it moves to a new slot. The Extreme preset has 34 exterior bodies and one ball; an all-plain cube has 26 exterior bodies and one ball. Both retain 26 abstract identities.

The optional older models retain procedural geared parts from `scripts/gear_meshes.py` and the `*-six-teeth-*` alternatives. Twelve-tooth pinions use 60° local spin; six-tooth pinions use 300°. Their plain domain also uses the new supplied mesh, so the plain-edge choice is consistent across all frontend presentations. Saved sessions record `gearModel: "supplied"`, `"12"`, or `"6"`; sessions saved under these corrected rules remain loadable. The revised coupling and world-frame convention change semantic digests; older Gear Cube saves are rejected against the new presets, rather than replayed with different rules. Continuous tooth contact and clearance are still unverified.

## Verification and spherical gate

Native tests cover fully geared, plain, Extreme, and mixed configurations; twelve-turn closure; inverse sequences; occupancy; pure blocking; all 0–4 middle-slice gear counts; dynamic mixed-slice legality; opposite-face motion; stationary middle carriers; shared component identity; circular motion about a non-origin pivot; intermediate motion; endpoint continuity; and rejection of an incorrect cog track. Native/WASM parity compares assets and frames. The external native and JavaScript consumers load articulated realizations without the reference application. Browser checks cover configurations, blocked and legal requests, synchronized animation, selection, and history.

Run `python3 scripts/gear_normal.py` to check the supplied realizations; use `--write` to regenerate them from `normal-meshes.json` and `plain-edge-meshes.json`. `python3 scripts/gear_model.py` checks the older models (including the supplied plain edge) and phase diagrams. Both run in `npm run check:packages`. The spherical candidate audit is `python3 scripts/gear_spherical_audit.py`.

A genuine S2 gear surface mechanism remains a separate construction gate. The existing cube regions fail a reproducible overlap audit when given the gear rotations. No spherical Gear Cube preset is advertised. See [the feasibility result](gear-spherical-feasibility.md).
