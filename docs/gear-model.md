# Experimental configurable Gear Cube

The **Gear Cube**, **Gear Cube Extreme**, and **Gear family · all plain** presets use the existing C++ `finite-placement-relations@1` backend. Their Euclidean and diagram views share a session. The gear checkboxes select which persistent edges have a cog; changing a checkbox creates a new solved definition and resets history. The configuration applies to piece identities, not fixed locations. **Edge model** switches between twelve-tooth and six-tooth geared edges while preserving the current state, selected piece, history, and cameras. The choice also applies to custom edge configurations and is saved in session presentation data.

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

The Euclidean mesh is now an editable contact-shape prototype, authored in `scripts/gear_meshes.py`. Edge cogs have twelve tooth profiles and a roof formed by the two adjacent cube faces; the two colored patches spin with that roof. The fixed carrier, bearing, and axle are separate components, and a hexagonal hub rides with the cog. Corner bodies have complementary toothed seats around their three neighboring edge axes. Centers have chamfered caps, and plain edges use smooth two-face replacement shells. Select **Animation → Inspect (3 s)** to examine the motion slowly. These shapes are intended for direct browser inspection; the seats and clearances have not been certified through complete move paths. [The original Gear Cube photographs](https://www.cs.brandeis.edu/~storer/JimPuzzles/ZPAGES/zzzGearCube.html) were used as visual references; the meshes are procedurally authored here.

The additional **6 teeth · 120° spin** model retains the corner meshes and their existing tooth profiles. It keeps every other edge tooth, preserving the original tooth width and corner seats. Per coupled move, the cog, its two colored patches, and its hub spin 120 degrees relative to the carrier in the same direction as the twelve-tooth model's 60-degree spin, as requested from the user's physical cube. Carrier and layer motions are shared by both models. Resting frames and signed animation tracks use the same phase-to-angle mapping in the Euclidean and diagram realizations, including inverse moves and phase wraparound.

The `*-six-teeth-euclidean.json` and `*-six-teeth-port-diagram.json` documents pin the same definition digest as the original models. The abstract catalog still has six phases: the new geometric map is `phase × 120°`, so phases differing by three share a resting geometric frame for a fixed carrier orientation. The core and placement inspector retain the distinct exact phases. This interpretation does not establish a one-to-one correspondence between the full abstract catalog and physical orientations; physical state quotients and continuous tooth contacts remain subjects for review.

## Verification and spherical gate

Native tests cover fully geared, plain, Extreme, and mixed configurations; twelve-turn closure; inverse sequences; occupancy; pure blocking; shared component identity; circular motion about a non-origin pivot; intermediate motion; endpoint continuity; and rejection of an incorrect cog track. Native/WASM parity compares assets and frames. The external native and JavaScript consumers load articulated realizations without the reference application. Browser checks cover configurations, blocked and legal requests, synchronized animation, selection, and history.

Run `python3 scripts/gear_model.py` to check authored packages; use `--write` only when accepting regenerated packages. The spherical candidate audit is `python3 scripts/gear_spherical_audit.py`.

A genuine S2 gear surface mechanism remains a separate construction gate. The existing cube regions fail a reproducible overlap audit when given the gear rotations. No spherical Gear Cube preset is advertised. See [the feasibility result](gear-spherical-feasibility.md).
