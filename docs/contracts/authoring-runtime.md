# Authoring and interpreter contracts

Status: Phases 1–5, 2026-10-31. Abstract artifact 1 and interpretation 1 remain
supported. [Interpretation 2](../../schemas/interpretation-v2.schema.json),
[realization envelope 1](../../schemas/realization-envelope.schema.json), and
[backend payload 1](../../schemas/backend-payload.schema.json) extend the
source-free path to the existing geometric backends. The
[abstract schema](../../schemas/abstract-artifact.schema.json) also supports the
required `grouped-action-records-v1` extension for large numeric programs.
Artifact API 1 is separate from legacy kernel API 7; see the exact
[API/ownership/error contract](../kernel-api.md#interpretation-2-and-realization-envelope-1).
The original drafts below retain design context. Resolved controller artifacts
and `controlDigest` remain proposals and are not accepted by the runtime.
The [architecture review](../architecture-review-authoring-contract.md) records
the remaining migration sequence.

The [authoring CLI](../authoring-tool.md) implements project contract 1 and build
receipt 1, with explicit native hosts, staged generation, source-free export and
native realization validation. Its separate tool package and detached consumer
checks exercise reproducibility, bounded dependencies, read-only checks and
publication failure handling. These host-side versions do not change any kernel
API, puzzle schema or buffer contract. Reference application/distribution
migration and presentation cleanup remain Phases 6–7.

The compiler produces immutable executable data. The abstract runtime validates
and executes that data. Geometric interpreters validate a separate correspondence
manifest and realization, then consume exact states and completed transitions.
Only the abstract executor decides permission, validity, and solvedness. Geometry
must remain outside its dependencies. The native/WASM compiler remains separately
usable when the authoring CLI is introduced.

| Draft schema | Data owner and identity |
| --- | --- |
| [Abstract artifact](abstract-artifact.schema.json) | Compiler → core/runtime; `executionDigest` |
| [Interpretation manifest](interpretation.schema.json) | Authoring → interpreter; pins execution, defines `interpretationDigest` |
| [Resolved controller](controller.schema.json) | Authoring → portable controller; pins execution, defines `controlDigest` |
| [Realization envelope](realization.schema.json) | Authoring → backend validator; pins interpretation and optional controls, defines `realizationDigest` |
| [Common types](common.schema.json) | Structural IDs, indices, content hashes, capabilities, and resource references |

The executable uses the existing [numeric compact schema 2](../../schemas/compact-v2.schema.json)
for its transport payload, always in the rectangular `positionMaps` form. This
reuse does not import a source-language dependency: the payload describes finite
groups/actions and numeric transport constructions. A compiler may emit earlier
source languages into this new representation. Source-only collection `id`
fields are excluded. All eight compact arrays are explicit, including empty ones.

**Executable representation.** The envelope requires `kind`, `artifactVersion`,
`requiredCapabilities`, `executionDigest`, and `program`. There is no source AST,
geometry, producer metadata, or diagnostic storage metric in this object.

The `program` carries predicate version 1, transport encoding version 1,
piece/placement counts, initial placements, sorted finite sets, predicate nodes,
child references, invariant/goal roots, action records, request programs, and the
compact transport payload. Its representation follows `core::ProgramData` rather
than C++ object layout. Derived lookup tables, caches, and pointers are omitted.

| Numeric reference | Index space and validation |
| --- | --- |
| Piece and placement | Zero-based indices into the declared piece/placement counts. Initial placement count equals piece count. |
| Predicate root | `0` is false, `1` is true, and root `i + 2` names `predicateNodes[i]`. Child dependencies obey existing core ordering checks. |
| `IN.input` and `IN.set` | Input is a state-piece or request-choice-slot index according to `bank`; set indexes `finiteSets`. |
| `RANGE.firstChild/childCount` | A checked slice of `childReferences`; all referenced predicate roots must exist. |
| `actionRecords.map` | Index into `compact.maps`; event is an opaque numeric identity, never an angle. |
| Request action slice | `firstAction/actionCount` indexes `actionRecords`. Each choice dimension has its declared finite size. |
| Compact reference | Each reference belongs to its named collection, group, action, space, position-map, binding, family, or transport array. Distinct spaces are not interchangeable even if their sizes match. |

`bindings` is optional for core-only use. When present, its ordered `pieces`,
`placements`, `events`, `choiceSlots`, and `choiceValues` define stable symbolic
identities for the corresponding numeric spaces. Its request array matches
`requestPrograms`; request/profile IDs are stable identities, profile choices are
numeric tuples, `whenRoot` is a state-only predicate, and `defaultProfile` is a
profile index or explicit `null`. Request shapes must agree with these global
choice bindings. A general program with request-specific shapes can omit this
source-language-style facade and use the numeric core directly.

The optional `session` section requires bindings and complete reciprocal request
and local-choice inverse tables. Index/table shape checks are mandatory; declared
inverse names are not proof of a universally invertible relation. Undo must still
plan the recorded inverse tuple and verify restoration of the exact previous
state. Profiles remain candidate selection policies, never permission proofs.
Bare programs require neither inverse tables nor drawable realizations.
Large programs may declare `grouped-action-records-v1` and replace action records
with ordered `{pieces, map, selectionRoot, event}` groups. Expansion preserves
record order, selection and participation exactly; every numeric action reference
still addresses the expanded sequence. The extension is required explicitly, so
older readers reject it before interpreting a different representation.

**Interpretation representation.** `domains` defines opaque domain and port IDs
and an explicit numeric `choiceSlot` for each domain;
`resources` defines opaque footprint identities. `pieces` and `placements` contain
explicit global core indices and domain indices. Piece port labels bind materials;
placement ports bind resources and opaque attachments. Arrays must cover each
declared piece and placement once, sorted by their global index. Domain IDs do
not imply a geometry, and strings such as `R` or `R.clockwise` have no special
runtime interpretation.

A certificate declares `{id, domain, request, event, transport, actions, unavailablePlacements}`. Its action
indices must exist and agree with its piece domain, event, and transport. Every
supported action must have an unambiguous certificate. Event numbers need not be
globally unique across domains or transports: the validated completed tuple
determines active action records. The runtime witness validator, rather than a
geometric endpoint guess, resolves that context. The first adapter additionally
requires direct-choice selectors, concrete default profiles and unique
domain/event correspondence; it rejects broader programs for geometry while
leaving bare-core execution available. It rejects ambiguous certificates.
Interpretation 2 adds an explicit numeric `choice` to each certificate and resolves
participation from the completed tuple and certified piece scope. Event names can
repeat across choices. One certificate is permitted per domain/local choice;
its representative request must select that choice. Empty action scopes are
allowed only for virtual domains with no abstract pieces, preserving geometric
carrier checks without creating abstract participation.

The manifest does not contain a geometric path. In the general envelope,
each selected realization maps
certificate indices to backend `pathId` values through `eventTracks`. This permits
different realizations of the same exact transport. Bindings must retain moving
self-loops, ordinary stationary pieces, and component-specific participation.
`unavailablePlacements` explicitly names nonparticipating placements outside the
certified frame section. These must belong to the same domain and be disjoint
from the transport's source support. This preserves the current interpreter's
stationary/participant/unavailable distinction without adding permission rules.
Source adapters may retain conveniences for face names and direction suffixes,
but must lower them into explicit data before export.

The implemented `artifact-cube-euclidean` backend instead supplies an explicit
axis/angle track per request and checks every endpoint/port against certificates.
Its realization identity hashes the exact input payload bytes. This narrow
backend does not accept the general envelope or resource resolver described below.
It retains the legacy cube resource/face/port and fused-bandage conventions;
those backend constraints do not affect the independently loaded abstract core.
The general payload adapter instead supplies an explicit per-piece geometric
kind and global frame bindings. The original three-argument entry remains compatible.

**Controller representation (proposal).** Each driver names a core piece, ordered stable
stop IDs, and explicit global-placement-to-stop-index rows. A family references
driver indices. A route contains source/target stop tuples in family-driver order,
a request index, the already resolved completion tuple, a stable path ID, and an
explicit preferred flag. It contains no source profile reference or angle.

Validate tuple sizes and bounds, unique phase-reader rows, stop coverage,
source/path uniqueness, and reciprocal inverse routes. Phase readers query exact
state. Every dispatch is still revision checked and planned through the core;
before commit, verify that the planned result reaches the requested stop tuple.
Missing phase coverage is an explicit unavailable phase, not a guessed angle.
Each `movements` record resolves a `(slot, value)` to compiled action indices.
Piece-to-family matching uses those actions' piece scopes and exact transport
support, without reading source moves or running every destination as a legality
query. Draft 1 supports the direct choice-membership action selectors emitted by
the current compiler; validate that these records exactly match those selectors.
A general program requiring other support analysis can still run through the
core, without this optional controller facade. Validate full support coverage
for the completion tuples referenced by controller routes, including idle tuples.

The controller's path identity and a realization's internal path IDs are separate
namespaces; a backend providing control-layout hints must declare their mapping.

**General realization representation.** The envelope declares a backend ID/version,
required capabilities, explicit event tracks, and a content-addressed payload.
The backend owns the payload schema and its interpretation of assets, frames,
motion families, pivots, winding, paths, and intrinsic space. Envelope validation
does not make an arbitrary backend payload valid. The current R³, spherical, quotient-surface and S³/SO(4) interpreters accept
this through the explicit payload adapter; backend registration remains static.

The payload reference contains a relative resource path and SHA-256 of its exact
bytes. The loader receives those exact bytes explicitly from the host; this
contract grants no filesystem or network access. Paths consist of slash-separated
ASCII file-name segments, exclude `.`/`..`, and have no leading/trailing slash,
backslash, URI scheme, or empty segment. Verify the content hash before parsing.
Payload dependencies must be explicit and content-addressed in the backend schema;
a backend must reject unverified implicit external references.

Require full coverage for the backend's declared supported action scope. Every
selected track must reach its certified target placement/ports, modulo declared
symmetries and quotient identifications only. Preserve signed/full winding and
local/world multiplication order, even for equal endpoints. The backend must
declare intrinsic coordinates, metric, matrix convention, and buffer versions in
its public descriptor; a 4×4 SO(4) matrix is not a homogeneous R³ matrix.

`controlDigest` remains reserved for a future controller integration. The current
envelope rejects it and omits driver-layout hints from exported payloads. Presentation labels/notation/shortcuts
are outside these four artifacts. Initially they remain existing application data;
a separately versioned presentation document can follow in Phase 7.

**Canonical bytes and identities.** These four envelopes contain only objects,
arrays, Unicode strings, booleans, nulls, and unsigned integer-valued numbers.
Coordinates and other floating-point payloads live in the separately hashed
backend resource. The integer-only envelope deliberately avoids choosing a new
cross-language floating-point canonicalization scheme.

For draft 1, canonical encoding is UTF-8 JSON without a BOM, whitespace, or final
newline. Object property names are the schema's ASCII fields, in lexicographic
ASCII order; array order is significant. Encode integer values in decimal without
leading zeros or exponent notation; zero, including parsed negative zero, encodes
as `0`. Decoder inputs must have exact integral values within the specified range;
do not truncate a fractional or overflowing value. Strings preserve Unicode scalar
values and case without normalization, emit non-ASCII UTF-8 directly, escape quote
and backslash, use `\b`, `\f`, `\n`, `\r`, `\t`, and use lower-case `\u00xx` for
other control characters. Reject malformed UTF-8 and unpaired surrogates. Encode
booleans/null with their JSON literals. Duplicate object keys are rejected before
constructing a DOM, rather than silently keeping the last value.

Compute each identity as SHA-256 of its canonical envelope with only that
envelope's own digest field removed. Retain all upstream digest fields. Omitted
optional sections stay omitted; there are no implicit hash defaults. Producers
sort required capability lists lexicographically and emit explicitly specified
arrays/fields. Schema-valid but noncanonical input may be normalized by the
decoder, but the supplied digest must match that normalized representation.

- Execution covers program, stable bindings, completion policy, inverse tables,
  versions, and required capabilities. It excludes authoring provenance and
  geometric/presentation data.
- Interpretation covers the complete correspondence and the upstream execution
  identity. Controls independently cover readers/routes and execution identity.
- Realization covers its envelope, upstream interpretation/control identities,
  and exact payload content hash. Whitespace or floating-point spelling changes
  in a payload change realization identity by design, without changing execution.
- Producer provenance and the output inventory have separate byte/content hashes.
  These hashes do not prove mathematical equivalence or physical correctness.

[Hash vectors](hash-vectors.json) provide portable reference bytes and SHA-256
values. The small JavaScript encoder in
[`tests/architecture/contracts.mjs`](../../tests/architecture/contracts.mjs) is a
test reference, not a second runtime loader. The shared C++ artifact loader checks
the same vectors in `artifact_contract` before publishing an executable handle.

**Limits, errors, ownership, and validation.** Indices are nonnegative exact
integers below `2^32 - 1`; that value is reserved. Optional indices use null or
explicit variant fields rather than negative/sentinel wire values. IDs contain
1–256 UTF-8 bytes, exclude NUL, and are unique within their declared namespace.
JSON Schema's character-count bounds are supplemented by byte-count validation.

The implemented parser limits each contract/payload to 64 MiB, nesting depth 64,
and a conservative 256 MiB DOM allocation budget. Existing core limits also bound
numeric program data. Proposed externally resolved backend resources have a separate
declared budget, initially at most 256 MiB in total per realization. These are
additional loader budgets, not requests to expand existing core limits. Enforce
`core::Program`'s piece, placement, node, reference, finite-set, map, action, request,
and work limits as well as aggregate counts; per-array bounds alone are insufficient.
Check products and `first + count` without integer overflow before allocation.

Unknown kinds, unsupported versions, and unknown required capabilities fail
explicitly. Unknown fields fail under these schemas. There is no implicit plugin
download or source recompilation fallback. Known optional sections may be absent;
present sections are fully validated before exposing a handle. An unsupported
backend does not invalidate an independently loaded core program.

| Proposed stable error code | Meaning |
| --- | --- |
| `artifact.unsupported` / `artifact.capability` | Kind/version or a required executable capability is unsupported. |
| `artifact.limit` / `artifact.invalid` | Byte/allocation/work budget or malformed structural data. |
| `artifact.reference` / `artifact.digest_mismatch` | Missing/out-of-range/inconsistent correspondence or wrong canonical identity. |
| `interpretation.invalid` / `interpretation.ambiguous` | Wrong transport/action/port coverage or multiple inconsistent certificates. |
| `realization.capability` / `realization.payload_mismatch` | Unsupported geometry feature or resource bytes differ from their content hash. |
| `realization.witness` | A transition does not equal the exact completed abstract witness. |

Native exceptions and WASM JSON diagnostics include code, JSON pointer source,
and message through the existing diagnostic contract; a dedicated stage field
remains a proposal. Validate first, publish an immutable handle second. Failure preserves
the live session and current drawing. A retained artifact owns its data; loading
must not depend on the caller keeping source text or resource buffers alive.
Frames/animations/drawings share immutable backing storage and can outlive their
creator. Preserve documented WASM view invalidation on memory growth and provide
copying helpers. Scheduling, easing, camera state, and renderer objects stay with
the host.

Old `definitionDigest`, formats 1–3, witness version 1, session schema 2, and public
buffer versions retain their current meanings. Do not compare a new execution
identity directly to an old definition hash. Conversion must verify an old save
against its matching old definition, replay recorded choices, map stable IDs,
and write an explicitly versioned new save. Native/WASM contract changes require
updated declarations and independently installed consumers.

**Executable acceptance gates.** `npm run check:architecture` validates the draft
structures/hash vectors and compares seven existing puzzle scenarios using an
installed native consumer and a fresh WASM process per scenario. It freezes exact
canonical-definition hashes, completed-witness hashes/requests/participation,
state snapshots, saves and saved redo tails. Helicopter and Melty also freeze
phase queries, piece-to-family matching, resolved intent previews, and stale
controller dispatch rejection. It compares geometry descriptors
exactly and representative pose samples with absolute-plus-relative tolerance
`1e-8 * (1 + abs(reference))`. Each consumer additionally checks complete endpoint
pose arrays, seek independence, stale revisions, and rejection of incomplete
witnesses; the Bandage scenario includes a blocked move. No exporter is invoked.

The baseline's original fixtures remain legacy inputs intentionally. Later
source-free loading must produce matching normalized logical outcomes from those
scenarios; API/hash migrations require explicit updates instead of hiding changes
in a new recorder run. This is representative coverage, not all 48 realizations.
Existing full parity, controller, geometry, and installed-SDK checks remain needed.

`npm run check:artifacts` adds independent compiler-free native/WASM consumers,
schema validation, deterministic export checks, legacy source parity and opaque
identifier checks. The native `artifact_contract` suite adds semantic rejection
fixtures for predicate cycles, choice-dependent profile predicates, bad slices,
invalid group tables and rectangular correspondences, noninjective transports,
invalid profiles/defaults/inverses, certificate/action/event mismatches and
ambiguous events, missing resources, wrong endpoints, unknown required
capabilities, duplicate raw JSON keys, and parser depth limits. Self-loop
participation, full winding, and seekable paths are also checked. Externally resolved paths,
general controller routes and broader external-resource budgets remain acceptance gates
for the later adapters. Schema acceptance or a matching producer hash never
substitutes for runtime validation.

To capture a deliberately reviewed new baseline, use
`npm run check:architecture -- --record`. Ordinary checks never rewrite fixtures.
Timings and peak RSS are unpinned observations in
`build/architecture/measurements.json`; they are not performance assertions. See
the [initial measurement record](baseline-measurements.md) for method and limits.
