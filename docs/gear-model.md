# Experimental configurable Gear Cube

The normal **Gear Cube** uses the supplied low-poly model, created in a separate ChatGPT conversation and converted into the project's existing triangle-mesh JSON format. The original GLB and its importer are not retained. This replaces the previous Boolean-construction investigation and viewer. **Gear Cube Extreme** and **Gear family · all plain** retain the earlier mesh prototypes; adapting the new model to them is deferred.

All three presets use the existing C++ `finite-placement-relations@1` backend. Euclidean and diagram views share a session. The gear checkboxes select which persistent edges have a cog; changing a checkbox creates a new solved definition and resets history. The configuration applies to piece identities, not fixed locations. Extreme and custom configurations keep the **Edge model** selector for twelve-tooth or six-tooth prototypes; switching preserves state, selection, history, and cameras. The normal preset always uses the supplied six-tooth model and its corresponding phase diagram.

## Exact rules and configuration

A `gear-cube-prototypes` source has schema version 1, a semantic version, a puzzle ID, and a `gearedEdges` array containing any distinct subset of `UF UR UB UL DF DR DB DL FR FL BR BL`. The native/WASM compiler generates the same exact finite definition as the packaged authoring check. No coordinates, floating-point angles, meshes, or camera data enter the abstract definition.

There are 26 persistent pieces: eight corners, twelve edges, and six centers. A geared edge is one composite identity; its carrier and cog are visual components, not duplicate core occupants. Geared placements retain a carrier orientation and one of six exact cog phases. Geared and plain placements claim the same edge-location occupancy resources. Imported assignments cannot place a carrier assembly and a plain edge in the same slot. Featureless center orientation is quotiented, with unchanged participating centers retained in transition witnesses.

The current **uniform-middle-slice-v1** policy models coupling as a bandage-style constraint:

- `F` turns the front outer layer 180 degrees clockwise, its middle slice 90 degrees, and the opposite outer layer zero degrees. Middle-slice cogs advance one signed abstract phase step, whose angle belongs to the realization. All four edges currently in that middle slice must be geared.
- `Fq` turns only the front layer 90 degrees clockwise. All four middle-slice edges must be plain. Geared edges in the outer layer ride with the layer without local spin.
- A mixed middle slice blocks both requests. Blocking names an implicated persistent edge. Inverses use an apostrophe; numerical suffixes repeat primitives.

Face keys choose a legal geared or ordinary request at dispatch; explicit notation retains the distinction between `F` and `Fq`. These rules cover all geared masks explicitly. They do not establish that every mask corresponds to a manufactured mechanism. The model remains **experimental**, including the contact geometry and cog-spin convention. Shape-derived phase-dependent collisions and independently reviewed physical constraints are further verification gates; the existing capacity-one resource backend can represent additional placement exclusions without adding geometry to the runtime core.

[Jaap's Gear Cube analysis](https://www.jaapsch.net/puzzles/gearcube.htm) provides the outer/middle coupling and twelve-turn behavior. [Jim Storer's firsthand description](https://www.cs.brandeis.edu/~storer/JimPuzzles/ZPAGES/zzzGearCube.html) records the 60-degree cog increment. [Jaap's Extreme analysis](https://www.jaapsch.net/puzzles/gearcube2.htm) provides the eight-geared/four-plain reference and ordinary turns along the plain slice. These are factual references, not borrowed implementations. Published total state counts use additional visual and whole-puzzle quotients; the explicit carrier/phase state has not been equated with those totals.

Examples:

```text
Gear Cube:          R U F (R U F)'    R12
Gear Cube Extreme:  Uq R F (Uq R F)'
All plain:         Rq Uq Fq (Rq Uq Fq)'
```

## Articulated geometric interpreter

The reusable C++ interpreter adds `articulated-euclidean` and `articulated-port-diagram`. An authored mesh can be non-convex, allowing a toothed cog. Every rigid component has a resting frame for each placement and an ordered list of simple rotations for each participating operation/source placement. Rotations specify an axis, pivot, signed angle in radians, and `world` or `local` space. World rotations multiply on the left; local rotations multiply on the right. All steps use one shared eased progress value, so local cog rotation accompanies carrier motion continuously. Inverse tracks preserve direction and winding.

Registration checks rigid frames, mesh indices, complete domain/port bindings, participant-track coverage, and endpoint agreement for every declared transport. Declared label-preserving mesh symmetries allow featureless center orientations. The diagram uses explicit schematic translations between chart positions and local marker rotation to expose cog phase. It is not a mechanical motion model.

Three.js draws supplied assets and per-component matrices, using the same selection, picking, destination preview, and history controller as existing puzzles. It contains no gearing calculations. Custom renderers use the same portable API and buffer layout. See [the API contract](kernel-api.md).

## Supplied normal Gear Cube model

The supplied model had five shared geometry prototypes: 8 corners, 6 centers, 12 edge bases, 12 six-tooth pinions, and 1 internal ball. The exterior has the requested 38 physical bodies. Each extra edge base and its pinion share one persistent abstract edge identity and occupancy resource, preserving the existing 26-piece exact definition and saved sessions.

The supplied triangles and colored surface regions are stored in [normal-meshes.json](../packages/gear/normal-meshes.json) without reshaping. Its 13 assets use the existing `vertices` and `triangles` representation, separating body and color patches. During conversion, a proper global rotation mapped `(x,y,z)` to `(x,z,-y)`, placing white at U and green at F; a uniform scale of 1.48 matches the reference application's cube size. The home assembly was compared against every source surface and color region before removing the binary. Source provenance, including its SHA-256, is recorded alongside the meshes.

`scripts/gear_normal.py` combines these assets with the existing placement-frame and rotation-track authoring and emits [gear-normal-euclidean.json](../packages/gear/gear-normal-euclidean.json). Loading uses the ordinary public native/WASM geometry APIs and triangle buffers. There is no separate binary-model loading path. The interpreter computes its usual flat triangle normals, and face labels use the frontend's palette.

Each edge base and its colored patches follow the carrier frame. Each six-tooth pinion and its colored patches additionally spin 300° (5π/3) relative to that frame per coupled move, using the agreed 20:6 ratio through a 90° driving step. Both motions share the interpreter's eased clock; inverse tracks reverse the full signed travel. A local port can have separate patches on the base and pinion, keeping one color label and hit binding despite their different motion. The internal ball is a single frozen body component bound to `center/U` for picking; it has no separate abstract identity and does not rotate with the center. Its body-only `pieceIds` filter prevents duplicate copies.

The abstract catalog still has six phases: `phase × 300°` modulo 360° gives six resting frames for a fixed carrier orientation. Six signed phase steps complete five full cog revolutions. The imported realization pins the existing normal definition digest. Select **Animation → Inspect (3 s)** for direct browser review. The source explicitly sets `motion_clearance_verified: false`; neither the import nor endpoint checks certify continuous tooth contacts or physical clearances.

Extreme and custom configurations retain the procedural meshes in `scripts/gear_meshes.py` and the `*-six-teeth-*` alternatives. Their twelve-tooth pinions use 60° local spin; six-tooth pinions use 300°. These are legacy shape prototypes, pending a separate model for Extreme.

## Verification and spherical gate

Native tests cover fully geared, plain, Extreme, and mixed configurations; twelve-turn closure; inverse sequences; occupancy; pure blocking; shared component identity; circular motion about a non-origin pivot; intermediate motion; endpoint continuity; and rejection of an incorrect cog track. Native/WASM parity compares assets and frames. The external native and JavaScript consumers load articulated realizations without the reference application. Browser checks cover configurations, blocked and legal requests, synchronized animation, selection, and history.

Run `python3 scripts/gear_normal.py` to check the normal realization; use `--write` to regenerate it from `normal-meshes.json`. `python3 scripts/gear_model.py` checks the legacy geometry and phase diagrams. Both run in `npm run check:packages`. The spherical candidate audit is `python3 scripts/gear_spherical_audit.py`.

A genuine S2 gear surface mechanism remains a separate construction gate. The existing cube regions fail a reproducible overlap audit when given the gear rotations. No spherical Gear Cube preset is advertised. See [the feasibility result](gear-spherical-feasibility.md).
