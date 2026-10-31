# Initial architecture baseline measurements

Captured 2026-10-31 at 12:52 China Standard Time, against runtime sources at
`9de6b11877026dcde39412d6aaf1d47f856c967f`. Exact input hashes and frozen functional
observations are in
[`tests/fixtures/architecture-baseline.json`](../../tests/fixtures/architecture-baseline.json).

This is one observation in a fresh child process per runtime/puzzle. Processes
ran sequentially on Linux ARM64, Node `v24.21.0`, with a 4 GiB cgroup budget.
The native libraries used the existing Debug configuration; WASM used the existing
optimized build. Filesystem caches were not cleared. These numbers establish a
reproducible measurement method and an initial reference, not a statistical speed
comparison between the two build configurations.

| Puzzle | Native load ms | WASM load ms | Native geometry ms | WASM geometry ms | Native peak RSS MiB | WASM peak RSS MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Cube | 25.68 | 29.54 | 229.41 | 59.36 | 9.7 | 126.1 |
| Bandage | 33.00 | 27.62 | 2.56 | 3.31 | 10.1 | 116.2 |
| Helicopter | 709.95 | 188.95 | 84.51 | 16.91 | 46.9 | 165.1 |
| Gear | 130.24 | 51.93 | 380.49 | 100.09 | 45.2 | 152.9 |
| Melty A | 1448.65 | 285.46 | 268.83 | 123.35 | 84.2 | 193.1 |
| Flat torus | 30.71 | 30.18 | 63.62 | 31.07 | 13.2 | 146.8 |
| S³ tesseract | 543.27 | 158.61 | 464.46 | 177.18 | 51.5 | 193.4 |

Load measures Session construction from package text, excluding file I/O, module
initialization, and later observation serialization. Geometry measures immutable
geometry-definition construction; Cube includes both Euclidean and spherical
views, while other rows use one view. Peak RSS is process-wide and includes runtime
startup, serialized definition/witness observations, and all views in that case.
It is not isolated kernel retained memory. The runner records WASM initialization
separately in its generated report.

The WASM file was 1,878,258 bytes, SHA-256
`b102b5e33a89526278431cfd1b9f318fa5d6c1e7164218e7c1cfc20254b7851f`.

| Puzzle | Input package bytes |
| --- | ---: |
| Cube | 132,381 |
| Bandage | 172,891 |
| Helicopter | 1,948,891 |
| Gear | 577,091 |
| Melty A | 3,064,120 |
| Flat torus | 186,725 |
| S³ tesseract | 923,680 |

Prepare dependencies with `npm ci`, configure the existing native preset with
`cmake --preset native`, and generate WASM with `npm run build:wasm` if necessary.
Then run `npm run check:architecture`. The runner builds/installs native libraries
and an independent consumer with one job, checks cgroup headroom, runs each case
in fresh processes, and writes the latest observations to
`build/architecture/measurements.json`. Timing/RSS differences never fail the
functional baseline. Use matching build configurations and repeated observations
when evaluating future performance claims.

Ordinary checks do not update frozen expectations. The explicit recording command
is `npm run check:architecture -- --record`; it requires native/WASM agreement for
every case before writing any fixture changes. Review changed inputs, definition
hashes, witnesses, saves, and poses before accepting a newly recorded baseline.

## Phase 6 distribution check

The final runtime-only consumer check on 2026-10-31 records its observations in
`build/runtime-check/measurements.json`. It uses one sequential Node process,
installed Debug native libraries and Release WASM with `-O1`. The compiler-free
WASM is 1,584,119 bytes; the full compatibility WASM is 2,296,287 bytes.
Module import/initialization took 35.15 ms in this run. Node peak RSS was
683.2 MiB across symbol inspection, all 24 runtime cases, 48 views and the
optional authoring/save-conversion check. It is not isolated kernel memory.

Per-puzzle `loadMs` measures construction after an independent validation pass;
it is a warm observation, with filesystem caches uncleared. The process also
holds copied native/WASM comparison results. These observations do not establish
a cold-start or throughput improvement over the original baseline. Run
`npm run check:runtime` to reproduce the method with matching packages.

## Phase 7 registry and presentation check

The 2026-10-31 runtime consumer check uses the same sequential installed-Debug /
Release-WASM (`-O1`) method. Runtime WASM is 1,587,660 bytes; full compatibility
WASM is 2,317,557 bytes. This run observed 21.80 ms for module initialization and
595.9 MiB Node peak RSS across archive inspection, all 24 presets / 48 views,
label-removal parity and optional source-save conversion. Initialization/load
measurements remain warm observations; peak RSS is not isolated kernel memory.
These single runs do not establish a performance improvement over Phase 6.
