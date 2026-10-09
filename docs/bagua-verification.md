# Bagua verification record

The current model passes independent geometric consistency checks and additional source-based behavior fixtures. It remains **experimental**. This record separates published expectations, model reproductions, and evidence that still needs a physical replay.

The reviewed semantic digest is `9e9c2c32a01e8aa431723e69df35e59f43e5778f3b377ce3e965d555b5c2a1e6`. These checks pin the unchanged puzzle definition and its 146 pieces. The spherical interpretation shares that same semantic identity and public native/WASM APIs.

## Published behavior

The expected effects below are transcribed from solving guides. They are not inferred from the generated transport tables. Source illustrations and model files are linked, not bundled.

| Reference | Simulator notation | Expected effect checked |
| --- | --- | --- |
| [Konrad, LLL-B1](https://sites.google.com/site/easytutorial3x3x3/bagua) | `[[U+ R2:U+],[R R+ L-:D2]]` | Three kite/triangle pairs cycle; the other 140 labeled pieces stay unchanged. |
| [ramon13, reply 1, kite formulas](https://twistypuzzles.ru/forum/index.php?topic=939.0) | `[[R2,U+ D-]4,F- B+ L2 F+ B-]` | Exactly three kites form one cycle; the other 143 labeled pieces stay unchanged. |
| Same source | `[[F-:[U' B' U,F]],[U-:D- R2 F2 R2]]` | The same pure-cycle property, via a different published construction. |
| [Spencer Parkin, steps 2–3](https://spencerparkin.github.io/twisty-puzzle-solutions/puzzle_bagua.html) | `X = (R2 U+ R2 U-)7` | The top-facing RU triangle moves to the top-facing RUB corner triangle region. Inverse X from solved creates two protruding slivers at RD through R, the setup described for X. |

ramon13 writes conjugation as `{A,B}`; the simulator spells it `[A:B]`. Both use `[A,B]` for commutators. Parkin uses `(1/2)U` for `U+`, `(1/2)Ui` for `U-`, and `2R` for `R2`; his seven repetitions become `(...)7`.

Native tests identify Parkin's triangle by its home surface region, then examine its resulting port centroid. They also check the complete unshrunk bodies: inverse X produces exactly two sliver protrusions, both at RD through R, while all other bodies fit within the cube. The prefix is a model reproduction of the described setup, not a separately recorded physical scramble. The portable fixture checks that its first repair move, R2, remains legal. Native/WASM tests independently assert the kite-cycle effects and compare full transition records.

The existing additional legal algorithms, inverses, blocking rollback, occupancy rejection, undo/redo, saved-history replay, and geometric animation checks continue to apply. Legal execution alone is weaker evidence than a specified resulting permutation.

## Geometric consistency

[`bagua_review_tests.cpp`](../tests/bagua_review_tests.cpp) reads the abstract definition and the separate Euclidean realization. It uses the unshrunk prototype vertices and placement frames, reconstructs supporting planes and boundary edge directions, and runs a separate separating-axis implementation. It does not import the Python authoring tool or use its precomputed overlap pairs as an oracle.

| Check | Scope | Result |
| --- | ---: | --- |
| Cut classifications | 2,232 labeled placements × 30 directed turns = 66,960 | Every stationary, participating and blocked role agrees. |
| Location aliases | All labeled placements grouped into 1,946 geometric locations | Aliases have identical solids and exclusions. |
| Pairwise occupancy | All 1,892,485 pairs of distinct locations | Shared abstract resources agree with positive-volume intersection. |
| Overlapping pairs | 51,876 | No missing or spurious conflict was found. |

The checker uses double precision with a `1e-8` tolerance; boundary contact is allowed. It cross-checks the exact `Q(sqrt(2))` construction rather than replacing it. This validates the exported ideal solids against their abstract rule encoding. It does not show that every occupancy-valid joint assignment is reachable, or that the solids capture every manufactured mechanism constraint.

## Physical blocking evidence and remaining gate

[David Guo's step 1](https://www.davidguo.idv.tw/Cube/BaGua.htm) contains a [circled physical photograph](https://www.davidguo.idv.tw/Cube/images/Bagua/Step1_1.jpg) identified as blocking L. The source does not provide the move history, all hidden orientations, or a complete labeled state. It therefore cannot serve as an exact replay fixture. It is not evidence that our model-selected prefix `U+ R F-` represents that photographed state.

Before promoting Bagua to a reviewed reference model, the remaining work is:

1. Independently review the complete piece construction and catalogs, including internal chirality and the center phases.
2. Record a reproducible move history from a known solved physical Bagua to a blocked state. Map its colors to `U D R L F B` and record the starting center-symbol orientations. Include the attempted face and turn angle, photographs from enough sides, and observations before and after undoing the last move.
3. Replay that same history in the kernel, compare the visible configuration and allowed/blocked requests, and check in the resulting fixture with its provenance. Review additional blocked states involving different piece types rather than generalizing from one photo.
4. Review any disagreement against the ideal cuts, finite catalog, and omitted hardware constraints before changing the semantic digest.

To capture the matching kernel session after recording a physical history, substitute that history for the first placeholder and the rejected move for the second:

```sh
build/native/twisty run --definition packages/bagua/definition.json \
  --algorithm '<recorded legal prefix>' --policy transactional \
  --save build/bagua-physical-replay.json --json
build/native/twisty run --definition packages/bagua/definition.json \
  --session build/bagua-physical-replay.json \
  --algorithm '<attempted move>' --policy transactional --json
```

The saved history verifies replay. The second command reports the kernel's decision and implicated pieces; its output must be compared with the physical observation, not used to supply that observation.

The [spherical Bagua realization](spherical-bagua.md) now reuses the existing piece identities and abstract rules. Its own coverage, transport and animation checks establish the geometric interpretation; the physical review limits above continue to apply.

## Run the checks

```sh
cmake --build build/native --target bagua_review_tests --parallel 1
ctest --test-dir build/native -R bagua --output-on-failure
npx vitest run tests/unit/parity.test.ts
```

Run compilation and tests sequentially. This audit avoids authoring regeneration and browser processes. Build jobs and test workers remain bounded by the workspace's 4 GiB memory policy.
