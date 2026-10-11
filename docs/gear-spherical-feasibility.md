# Spherical Gear Cube: construction gate

The requested target is a surface mechanism on S2 whose contacts and cuts realize the geared constraints. It requires a continuous partition and compatible gear contacts, including blocking. Radially projecting a Euclidean display does not establish this mechanism.

## Candidate tested

The first candidate reused the existing cube's six 70-degree disk regions. On the unit sphere a face disk is `n dot p > cos(70 degrees)`. The set of containing disks identifies one of the 26 center, edge, or corner regions. This candidate has complete, disjoint coverage at the solved state.

The audit applies one `R` geared primitive: rotate positive-X outer regions by −90 degrees and negative-X outer regions by +90 degrees, while middle carriers remain stationary. Each middle edge also spins 60 degrees about its initial radial axis, using the legacy twelve-tooth realization. These are proper rotations about the origin, so every sampled point stays on S2.

That sphere-preserving property is insufficient. `scripts/gear_spherical_audit.py` samples 4,096 deterministic directions and counts containing regions after inverse transport. The checked artifact is `packages/gear/spherical-audit.json`; it records a point and implicated pieces for each sampled overlap. The solved frame has zero gaps or overlaps. At half progress there are 113 uncovered directions and 109 multiply covered directions; at the endpoint there are 134 uncovered directions and 133 multiply covered directions.

This is a counterexample to **reusing the existing cube regions with cog spin**, not a proof that all spherical gear mechanisms are impossible. Mesh inset or clearance cannot repair missing conjugate contact boundaries.

## Required next construction

A new construction must split edge carriers and cogs into actual surface regions and derive conjugate tooth/contact boundaries from their relative rotations. It must maintain coverage and separation over each complete primitive path, establish the intended gear ratio through contacts, preserve labeled ports and exact resting phases, and demonstrate the blocked transitions. Mixed geared/plain configurations need compatible replacement regions rather than deleting a spinning surface.

Validate continuous paths analytically or with certified bounds. Sampled tests can disprove a candidate and catch regressions, but do not certify all intermediate configurations. Until these checks pass, the package provides Euclidean and diagram views only, with the existing cube/sphere default retained for other puzzles.
