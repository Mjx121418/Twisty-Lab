# Experimental Bagua Cube

This package models the DaYan Bagua Cube with six face axes and 45° stops. It is an ideal planar mechanism, available for independent review. The reference application offers a polyhedral cube and a labeled port diagram; the original cube still opens in **Cube + sphere**.

## Pieces and notation

The model has 146 persistent pieces and 198 colored local ports. Alexandru Popa's [Sun puzzle paper](https://www.researchgate.net/publication/380721263_Discover_and_Design_Sun_Twisty_Puzzles) describes the shared piece structure and explains that Bagua colors the Sun Cube's hidden pieces. Those sliver pieces have two exterior ports each.

| Domain | Pieces | Labeled placements |
| --- | ---: | ---: |
| Corner | 8 | 168 |
| Center | 6 | 48 |
| Center triangle | 24 | 744 |
| Edge triangle | 24 | 168 |
| Sliver | 24 | 264 |
| Left kite | 24 | 288 |
| Right kite | 24 | 288 |
| Edge | 12 | 264 |
| Total | 146 | 2,232 |

Left and right kites have different internal shapes even when their stickers look alike. The generator assigns these two domain names to its chiral prototypes. Every piece has a stable ID such as `kite-left/00`, independent of its current placement. Centers retain all eight orientation phases, so the home goal is sensitive to their exact orientation even though the reference renderer uses plain colors.

For each face `U D R L F B`, `U+` is 45° clockwise viewed from outside, `U-` is its inverse, and `U` is 90°. Ordinary notation `U'` resolves to the declared inverse `U_inv`; `U2` repeats the quarter turn. An additional `U_half` primitive performs 180° directly. These give 30 directed operations. Prime inverts an entire suffixed operation: `U+'` means `U-`. Face keys remain 90°; Shift chooses the declared inverse.

## Exact authoring and abstract rules

[`scripts/bagua_model.py`](../scripts/bagua_model.py) independently constructs ideal convex pieces using exact arithmetic in `Q(sqrt(2))`. The construction uses a cube from −1 to 1, layer depth `d = 9/20`, center-block depth `9/10`, and an edge-block trim `x + z >= 27/20` in a representative positive quadrant. Each incident face contributes the diagonal cuts `±x ±z = sqrt(2)*d`, with corresponding coordinate permutations. This yields five pieces per center block, nine per edge block, and one per corner block. The construction was cross-checked against the physical parameters and counts in the public pCubes Bagua model, programmed by Skallos; its author distributes pCubes through [this forum thread](https://twistypuzzles.com/forum/viewtopic.php?p=437998#p437998). No pCubes code, model file, artwork, or binary is bundled.

Starting from solved copies, the generator explores every permitted 45° single-piece transport until each labeled catalog closes. The combined catalogs contain 1,946 distinct geometric shapes. Distinct orientations that preserve a shape can still be different labeled placements. This is finite single-piece closure, not an enumeration of the reachable joint-state graph.

For a face normal `n`, an exact vertex test against `n dot x = d` decides the placement's role:

- A piece strictly on both sides blocks the cut.
- A piece entirely in the outer cap participates, including boundary contact.
- Other pieces stay stationary.

Rotation about `n` preserves this cut plane. Moving and stationary solids remain separated throughout a permitted turn, and a common rigid rotation preserves separation within the moving set. The mechanism thus permits the complete motion, not just its endpoints.

Exact separating-axis tests check both face normals and edge cross-products for every pair of distinct geometric locations. They find 51,876 positive-volume overlaps; touching is permitted. A deterministic complete-clique cover encodes these conflicts with 5,883 capacity-one exclusion resources, alongside 1,946 location resources. Each clique contains only mutually conflicting locations, and the cover includes every conflicting pair. Sharing a resource is therefore equivalent to the corresponding pairwise overlap prohibition.

The abstract definition contains only placement IDs, footprints, local-port attachments, blocked placements and exact transport tables. Coordinates, planes, floats, meshes and camera controls stay outside the abstract core. Bagua uses the existing `finite-placement-relations@1` module. The only notation extension is accepting `+` inside an operation token.

The C++ interpreter consumes separate realization documents. Polygonal stickers contain 3–8 model vertex indices here; they remain one abstract port each. The interpreter validates their supporting plane and convexity, sorts their boundary and triangulates them. It independently verifies all placement-frame transports. Three.js consumes shared assets and sampled transforms through the existing [kernel API](kernel-api.md).

## Independent acceptance targets

[`review.json`](../packages/bagua/review.json) pins the semantic digest, piece counts, external sequences, and a model-derived blocking fixture. It records experimental status and the remaining review scope.

The strongest external fixture is Konrad's [LLL-B1 pair cycle](https://sites.google.com/site/easytutorial3x3x3/bagua):

```text
[[U+ R2:U+],[R R+ L-:D2]]
```

It must produce two 3-cycles, moving three kites and their three triangles, while every other labeled piece stays unchanged. This expected effect comes from the solving guide. Further legal sequences include Konrad's TP-B2, R-B1 and cuboid algorithms, [Spencer Parkin's W](https://spencerparkin.github.io/twisty-puzzle-solutions/puzzle_bagua.html), and [Chris King's Sun commutator](https://dhushara.com/cubes/cubes.htm).

Native tests verify these sequences, inverses, eight-step closure, quarter/half-turn equivalence, a blocked jumbled cut, occupancy exclusions, transactional rollback, undo/redo and replay. Geometry checks cover unshrunk sticker area, polygon validation, picking, stationary pieces, continuous sampled frames and exact endpoints. Native/WASM parity and independent native/Canvas consumers exercise the same package.

The blocking example `U+ R F- U+` stops at the final turn with `corner/07` crossing the cut. This is a fixture derived from the specified ideal solids, not an independently photographed manufactured-puzzle state.

## Regeneration and memory

Build the native CLI first, then regenerate or check one package at a time:

```sh
cmake --build build/native --target twisty --parallel 1
python3 scripts/bagua_model.py          # Recompute and compare; no package changes
python3 scripts/bagua_model.py --write  # Rewrite after reviewing the model change
```

The exact overlap calculation takes a few minutes. Projection caches are bounded to 16 entries per hull, and the authoring process has a 384 MiB address-space ceiling. Progress reports its Linux process high-water RSS. Compact JSON keeps each document below the kernel's 4 MiB input bound. Native/WASM builds default to one job and browser tests use one worker. Fixture tests release each puzzle handle promptly. Heavy workloads run sequentially under the container's 4 GiB budget.

The ideal solids omit rounding, springs, friction, hardware and manufacturing tolerances. Full independent human review and a physically observed blocked-state comparison remain open. A spherical Bagua realization is a separate later milestone.
