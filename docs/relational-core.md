# Experimental relational language and flat executor

This implements the first execution milestone of the [migration plan](abstract-language-core-plan.md): an expanded JSON AST, a portable C++ compiler, a domain-free executor, and a separate completion helper. The existing simulator presets, sessions, geometric interpreters, and saved documents still use the existing API. Their migration follows after this foundation has been checked against the current puzzles.

The new path is available through `Twisty::flat_core` (standard C++ library only), `Twisty::relational` (compiler, completion helpers, and JSON facade), and the WASM `RelationalPuzzle` class. It is advertised as `experimentalAPIs.relational = 1`. Public API 1 and frame-buffer version 1 remain unchanged; replacing the existing session/definition contracts will require the planned API 2. Do not treat the experimental preview as a persisted compiled-definition or session format. It has no semantic digest or replay codec yet.

## Execution and completion

The executor takes a valid state, a request, and a **complete movement tuple**. It checks `R(s,u,a1,...,an)` and returns either a permitted atomic transition or rejection of that tuple. Checking involves no search or automatic rule propagation.

The compiler flattens the disjoint union of local catalogs. The execution data contains no domain names, piece types, or geometry. A state is one vector of global placement IDs, with per-piece placement compatibility compiled into the common invariant. Occupancy and other exclusions are authored invariants; they are not implicit runtime rules.

All predicates lower to `IN` and `RANGE` instructions. Constants are references 0 and 1; other references index `predicateNodes[ref - 2]`. `IN` reads a state or choice input and tests an interned finite set. `RANGE` counts true children through a packed slice of `childReferences`. Repeated children remain repeated counted terms. Nodes are shared and topologically ordered; evaluation uses an explicit stack rather than C++ recursion.

Selections decode the supplied choices. Local requirements and interaction clauses are tested on the original source, transports run simultaneously, and the same invariant is evaluated afresh on the tentative target. Missing transport rows mean no participation. Explicit self-loops retain participation and their opaque events. Multiple writers of a piece reject the tuple, even when their endpoints match. This AST uses one choice per domain and disjoint piece families, so its compiler structurally excludes conflicting local writers; the low-level executor also checks them defensively.

`bind_profile()` and `discover()` are outside the executor:

- Binding selects an explicitly named profile or an author-declared default whose state-only `when` predicate holds. Without either, it requests a choice. It never picks the first profile or claims a profile exhausts the relation. A selected tuple still needs execution permission.
- Discovery streams candidate tuples under separate candidate and result limits. `complete: false` means the search stopped early. An empty incomplete result proves nothing about the existence of a permitted tuple. `complete: true` means every tuple was checked. This is a small bounded exploration helper, not a general-purpose solver.

Distinct choices remain distinct even when their targets coincide. Every permitted transition includes its request, concrete choices, original and target states, and `{piece, from, to, event}` records. A renderer will eventually bind those event IDs to motion tracks; endpoints alone cannot determine winding.

## JSON AST version 1

The [complete example](../examples/relational/coupled.pzl.json) covers synchronization, cyclic implications, alternatives, obstruction, fixed profiles, and different winding events. The [JSON schema](../schemas/relational-puzzle.schema.json) describes the structural contract; the C++ compiler additionally checks names, types, supports, recursion, and bounds.

```json
{
  "kind": "relational-puzzle",
  "languageVersion": 1,
  "id": "two-components",
  "domains": [
    {
      "id": "corners",
      "placements": ["a", "b"],
      "pieces": [{"id": "corner-1", "initial": "a"}],
      "moves": [{"id": "swap", "support": ["a", "b"],
                 "transport": {"a": "b", "b": "a"}}]
    },
    {
      "id": "edges",
      "placements": ["a", "b"],
      "pieces": [{"id": "edge-1", "initial": "a"}],
      "moves": [{"id": "swap", "support": ["a", "b"],
                 "transport": {"a": "b", "b": "a"}}]
    }
  ],
  "requests": [{
    "id": "turn",
    "relation": {"all": [
      {"chosen": {"domain": "corners", "moves": ["swap"]}},
      {"chosen": {"domain": "edges", "moves": ["swap"]}}
    ]},
    "profiles": [{"id": "both", "choices": {"corners": "swap", "edges": "swap"}}],
    "defaultProfile": "both"
  }]
}
```

Domain order determines the companion choice-slot order. Each slot's movement values start with implicit `idle`, followed by its declared moves. `idle` cannot be redefined. Piece names are globally unique. Placement references in expressions use `domain/localName`; initial placements, supports, and transport targets use local names. Catalogs remain separate even when local names are identical.

A move applies its transport to all pieces in its domain, or to the optional explicit `pieces` subset. Its table must have exactly the declared source support, known targets, and unique targets. `requires` defaults to `true` and must depend only on state. `event` defaults to `domain/move`; it is an opaque name, not an angle or direction. A simultaneous composition within a domain can already be supplied as one explicit exact transport and event; automatic composition syntax is deferred.

Supported expressions are:

| Expression | Meaning |
| --- | --- |
| `true`, `false` | Constants |
| `{"at":{"piece":"p","placements":["d/q"]}}` | Placement membership |
| `{"occupiedBy":{"placement":"d/q","pieces":["p","r"]}}` | At least one named piece occupies that placement |
| `{"chosen":{"domain":"d","moves":["idle","m"]}}` | Supplied local choice membership |
| `{"request":["u","v"]}` | Request membership, specialized by compilation |
| `{"all":[...]}`, `{"any":[...]}`, `{"not":...}` | Boolean conjunction, disjunction, negation |
| `{"implies":[a,b]}`, `{"iff":[a,b]}` | Implication and equivalence |
| `{"count":{"min":k,"max":l,"of":[...]}}` | Inclusive true-term count, preserving multiplicity |
| `{"ref":"name"}` | A definition from top-level `predicates` |

Empty `all` is true; empty `any` is false; impossible count intervals are false. Named predicates must be nonrecursive. Cyclic *constraints*, such as `(a implies b) and (b implies a)`, are ordinary Boolean expressions and allowed. All names and operand types are checked even in branches that fold to a constant.

Top-level `relations: [{id, predicate}]` are conjoined with each request's `relation` and selected local requirements. The request leaf lets a relation refer to `u`. Invariant, goal, local requirements, and profile conditions are state-only; they cannot read a choice or request. The invariant defaults to placement compatibility alone. The goal defaults to the initial assignment and never restricts permission. Profile declarations select candidates and do not add clauses to the relation.

The AST is the initial expanded form. Textual `.pzl` syntax, imports, reusable domain modules, finite templates, symmetry expansion, automatic exact-profile derivation, source-level conflict compositions, and shipped-puzzle libraries are subsequent milestones. No runtime callbacks or puzzle-specific predicate handlers are used here.

## Native use

```cpp
#include <twisty/relational_completion.hpp>

auto puzzle = twisty::relational::compile_text(source_text);
auto state = puzzle.program.data().initial;
auto choices = twisty::relational::bind_profile(puzzle, 0, state);
if (choices) {
  auto plan = puzzle.program.plan(0, state, *choices);
  if (plan.transition) state = plan.transition->after;
}
```

`Program` validates its data at construction and exposes only const execution data. Each query owns its scratch; a const program can be shared across threads. The compiler package and its metadata should be treated as immutable. Native input/program errors throw `flat::Error` with a `code`. A well-formed but forbidden tuple returns a `Plan` rejection. No session state is mutated by planning, binding, discovery, or goal tests.

The independently linkable example accepts a source path and one JSON query per stdin line, or `--inspect` for the compiler preview:

```sh
cmake --preset native
cmake --build --preset native --target twisty_relational flat_core_tests
build/native/twisty_relational examples/relational/coupled.pzl.json --inspect
ctest --preset native -R 'flat_core_relations|relational_source_oracle'
```

The example's separate CMake project can also consume an installed `Twisty::relational` package. Linking only `Twisty::flat_core` adds neither the JSON compiler nor the existing executor/geometry.

## WASM and TypeScript use

```js
import { createKernel, ExperimentalRelationalPuzzle } from '@twisty/kernel';
const kernel = await createKernel();
const puzzle = new ExperimentalRelationalPuzzle(kernel, sourceText);
try {
  const state = puzzle.preview.program.initialPlacements;
  const candidate = puzzle.query({ op: 'bind', state, request: 'fixed' });
  if (candidate.status === 'Selected') {
    const result = puzzle.query({ op: 'plan', state, request: 'fixed', choices: candidate.choices });
    // Only result.status === 'Permitted' provides a transition.
  }
} finally { puzzle.dispose(); }
```

The low-level equivalents are `new kernel.RelationalPuzzle(sourceText)`, `inspectJSON()`, and `queryJSON(queryText)`. `compileRelationalJSON(sourceText)` returns `Compiled` with a preview, or `Invalid` with diagnostics, without retaining a handle. Constructors throw on invalid source. Query JSON errors return `Invalid`; an invariant-invalid source differs from a valid source with a rejected tuple.

Every query supplies `state: number[]`. The supported operations are:

| `op` | Additional fields | Result |
| --- | --- | --- |
| `validate` | None | `Valid` or `Invalid` |
| `goal` | None | `Evaluated`, `solved` |
| `plan` | `request`, complete `choices: string[]` | `Permitted` with transition, or `Rejected` with reason |
| `bind` | `request`, optional `profile` | `Selected` with candidate choices, or `ChoiceRequired` |
| `discover` | `request`, `candidateLimit`, `resultLimit` | `Discovered` with choices, `examined`, and `complete` |

Placement, piece, and event IDs in results index the companion bindings arrays. Choice names follow `bindings.choiceSlots`. Supplied choices must include every idle slot; the core does not infer omitted values. All returned JSON and JS arrays are owned copies. Call `delete()` on the low-level handle or `dispose()` on the adapter. There are no borrowed memory views or implicit animation clocks in this API.

Source text is limited to 8 MiB and nesting 256; expressions expand to at most 100,000 shared nodes, 1,000,000 child references, and 2,000,000 finite-table entries. Limits reject oversized expansion rather than enumerating full puzzle states. Discovery additionally limits candidates to 1,000,000 and returned tuples to 10,000. Production catalogs may require measured adjustments; these limits are not a guarantee that every finite relation has a small representation.

## Verification

The native C++ tests exercise atomic execution, input/program validation, conflicting writers, source/target predicate rebinding, self-loop participation, deep graphs, candidate binding, and truncated/exhaustive discovery. The independent Python evaluator reads the source AST directly and exhausts tiny global assignments and local tuples, including invalid cross-catalog imports. Its expected states and witnesses never use compiled predicates or maps. Seeded Boolean formulas and a separate occupancy model broaden the comparison.

After building native, WASM, and the packaged SDK, run `npm run check:relational`. Optional positional arguments select the native example executable and SDK entry point when using separate build directories. That check compares both the native consumer and WASM API with the source oracle, including complete transition witnesses and compiler previews.
