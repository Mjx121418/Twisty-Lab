# Spherical Gear Cube: supplied view and construction gate

The supplied spherical Gear Cube model is now available for normal, Extreme, all plain, and custom configurations through `articulated-spherical`. It uses the supplied S2 assembly's curved meshes, six-tooth pinions, and the existing exact state and motion tracks. See [model storage and conversion](gear-model.md#supplied-spherical-model). The interpreter verifies radial preservation and catalog endpoints; continuous coverage, contacts, and clearance are not yet certified.

A surface mechanism on S2 whose contacts and cuts realize the geared constraints requires a continuous partition and compatible gear contacts, including blocking. Radially preserving a display does not establish this mechanism. The historical audit below concerns an earlier disk-region candidate, not the supplied mesh.

## Historical candidate tested

The first candidate reused the existing cube's six 70-degree disk regions. On the unit sphere a face disk is `n dot p > cos(70 degrees)`. The set of containing disks identifies one of the 26 center, edge, or corner regions. This candidate has complete, disjoint coverage at the solved state.

The audit applies one `R` geared primitive: rotate positive-X outer regions by −90 degrees and negative-X outer regions by +90 degrees, while middle carriers remain stationary. Each middle edge also spins +60 degrees about its outward radial axis, using the legacy twelve-tooth realization and the corrected port-independent handedness. These are proper rotations about the origin, so every sampled point stays on S2.

That sphere-preserving property is insufficient. `scripts/gear_spherical_audit.py` samples 4,096 deterministic directions and counts containing regions after inverse transport. The checked artifact is `packages/gear/spherical-audit.json`; it records a point and implicated pieces for each sampled overlap. The solved frame has zero gaps or overlaps. At half progress there are 111 uncovered directions and 113 multiply covered directions; at the endpoint there are 135 uncovered directions and 130 multiply covered directions.

This is a counterexample to **reusing the existing cube regions with cog spin**, not a proof that all spherical gear mechanisms are impossible. Mesh inset or clearance cannot repair missing conjugate contact boundaries.

## Required next construction

A new construction must split edge carriers and cogs into actual surface regions and derive conjugate tooth/contact boundaries from their relative rotations. It must maintain coverage and separation over each complete primitive path, establish the intended gear ratio through contacts, preserve labeled ports and exact resting phases, and demonstrate the blocked transitions. Mixed geared/plain configurations need compatible replacement regions rather than deleting a spinning surface.

Validate continuous paths analytically or with certified bounds. Sampled tests can disprove a candidate and catch regressions, but do not certify all intermediate configurations. The supplied spherical view is available for review in the default **Cube + sphere** pair; these additional checks remain the gate for claiming a verified spherical gear mechanism.
