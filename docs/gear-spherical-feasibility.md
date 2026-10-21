# Spherical Gear Cube: model and verification goals

The supplied spherical Gear Cube model is now available for normal, Extreme, all plain, and custom configurations through `articulated-spherical`. It uses the supplied S2 assembly's curved meshes, six-tooth pinions, and the existing exact state and motion tracks. See [model storage and conversion](gear-model.md#supplied-spherical-model). The interpreter verifies radial preservation and catalog endpoints; continuous coverage, contacts, and clearance are not yet certified.

A surface mechanism on S2 whose contacts and cuts realize the geared constraints requires a continuous partition and compatible gear contacts, including blocking. Radial preservation alone does not establish these properties. The old disk-region candidate, its audit script, and its archived sample fixture have been removed.

## Remaining verification

The current carrier/cog surface regions need continuous contact and clearance checks over each complete primitive path. Verification must establish coverage and separation, the intended gear ratio through contacts, preserved labeled ports and exact resting phases, and the blocked transitions. Mixed geared/plain configurations need compatible replacement regions for unchecked pieces.

Validate continuous paths analytically or with certified bounds. Sampled tests can disprove a candidate and catch regressions, but do not certify all intermediate configurations. The supplied spherical view is available for review in the default **Cube + sphere** pair; these additional checks remain the gate for claiming a verified spherical gear mechanism.
