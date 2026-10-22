# Remaining puzzle-language work

This roadmap contains unfinished authoring and tooling work. Current execution
semantics and public contracts are documented in the [language guide](core-language.md)
and [kernel API](kernel-api.md).

## Textual `.pzl` frontend

- [ ] Define and implement declarative text syntax that lowers to the versioned
  JSON AST through the portable C++ compiler.
- [ ] Support qualified names and report syntax and symbol errors at their source
  locations. Apply bounded parsing and expansion in both native and WASM builds.
- [ ] Check equivalent text and JSON inputs against the same independent source
  evaluator, including complete movement choices and event witnesses.

## Imports, reusable modules, and finite templates

- [ ] Add general module instantiation, imports, finite parameters, templates,
  and loops for catalogs and repeated declarations. Reuse the finite symmetry
  library for group actions and coset expansion.
- [ ] Keep generated names qualified and deterministic. Diagnose import cycles,
  ambiguous names, invalid references, and excessive expansion.
- [ ] Verify that expansion produces the same finite program and bindings as an
  explicitly expanded source, without enumerating complete puzzle states.

## Named bundles and movement composition

- [ ] Provide source-level bundles for exact movement tuples and simultaneous
  compositions of local primitives within one choice slot.
- [ ] Define explicit transport and event semantics for compositions. Reject
  conflicting writes unless the author supplies an explicit composition; avoid
  declaration-order or implicit closure rules.
- [ ] Test participating self-loops, explicit idle choices, and different events
  with identical endpoints so composition preserves motion and replay meaning.

## Automatic completion profiles

- [ ] Derive guarded profiles or default selectors for tractable relations,
  including cases with a provably unique permitted completion.
- [ ] Keep synthesis outside the executor and validate every selected tuple
  against the permission relation. Report when a choice is required or synthesis
  is unavailable; preserve all other permitted alternatives.
- [ ] Check derivation against exhaustive tiny models with zero, one, and multiple
  completions, cyclic constraints, and non-monotone Boolean relations. A bounded
  unsuccessful search must not claim that no permitted completion exists.

## Source-mapped diagnostics

- [ ] Give authored requirements and relation clauses stable source IDs and map
  lowered predicates back to files, source locations, and expanded module origins.
- [ ] Explain rejected tuples using named pieces, placements, movement choices,
  and relevant membership facts or count bounds. Distinguish rejection of one
  tuple from unsatisfiability of an unresolved request.
- [ ] Keep source provenance in compiler metadata and preserve the separation
  between explanatory reason IDs and executable permission semantics.

## Optional rigid-bandage authoring helper

- [ ] Allow separately retained piece identities to declare rigid relative-placement
  compatibility and permitted movement combinations. Forcing pieces to move
  together alone does not establish a rigid bandage.
- [ ] Keep the existing compound-bandage preset's identities unchanged unless a
  new model is explicitly selected.

## Scope and acceptance

New authoring constructs must lower to finite catalogs, transports, and the
common predicate program. Keep geometry, renderer controls, and animation
scheduling outside the abstract language and executor. Validate changed
contracts through independent native/WASM consumers and preserve concrete
choices and events in session replay.

Infinite or symbolic placement algebras, continuous logical states, runtime
scripting, a general automatic relation solver, and an interactive authoring
editor are outside this roadmap.
