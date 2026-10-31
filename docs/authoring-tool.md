# Puzzle authoring tool

`twisty-author` owns offline model generation, project dependencies, staging,
export, validation, and publication. It uses Python 3.9+ and its standard library.
The C++ libraries retain compilation and geometric certification. The abstract
runtime, geometric interpreters, renderers, and controllers remain independent
of this executable. Runtime consumers need neither Python nor this tool.

From the repository:

```sh
npm run author -- build tools/authoring/projects/cube3.json --tools-dir build/native
npm run author -- check tools/authoring/projects/cube3.json --tools-dir build/native
npm run author -- inspect tools/authoring/projects/cube3.json --json
npm run author -- validate tools/authoring/projects/cube3.json --tools-dir build/native
npm run package:authoring
```

`package:authoring` copies the Python package, five native executables, and two
Cube example projects to `build/authoring-tool`. Copy that directory elsewhere;
Node, npm, CMake, and the source checkout are unnecessary for using it:

```sh
python3 tools/authoring/twisty-author build examples/source/project.json --tools-dir bin
python3 tools/authoring/twisty-author check examples/source/project.json --tools-dir bin
python3 tools/authoring/twisty-author build examples/preset/project.json --tools-dir bin
```

Native executables target the build machine's platform. To build hosts for a
native SDK installation, use `examples/language` and `examples/artifact` with
`TWISTY_BUILD_ARTIFACT_EXPORTER=ON`; the repository additionally builds the
offline `melty_solids` helper. Executables can be selected individually with
`--twisty`, `--twisty-language`, `--melty-solids`, `--twisty-artifact-export`, and
`--twisty-artifact-validate`, or discovered on PATH. Model implementations receive
explicit tool paths; only repository compatibility wrappers use `build/native`.

## Project contract version 1

`tools/authoring/project.schema.json` describes the manifest. A source project
supplies a relational package and one or more legacy realization documents:

```json
{
  "kind": "twisty-authoring-project",
  "projectVersion": 1,
  "id": "my-puzzle",
  "producer": {
    "type": "source",
    "package": "inputs/package.json",
    "realizations": [{"id": "solid", "path": "inputs/solid.json"}]
  },
  "output": "build/my-puzzle"
}
```

Input and output paths resolve relative to the manifest, including explicit
parent-relative references. `--output` overrides the destination relative to the
calling directory. Project and realization IDs are unique lowercase identifiers.
Unknown fields, duplicate JSON keys, missing files and unsupported project
versions fail. The CLI checks cross-field constraints beyond the JSON schema.

A `preset` producer selects one of the 24 reference models in `presets.json`.
Its project ID equals `model`; `inputs` maps exactly the selected preset's staged
seed paths to explicit manifest-relative files. Bundled manifests live under
`tools/authoring/projects`. Model modules and required bundled data are tracked
per selected preset, following relative Python imports. Unrelated models and
other presets' descriptor entries do not invalidate a build. Shared producer
modules and native binary changes do invalidate affected projects.

## Output and validation

A successful build publishes a dedicated directory:

- `abstract.json`: geometry-free, source-free execution artifact.
- `interpretation.json`: version 2 correspondence manifest.
- `controls.json`: resolved controller contract 1, pinned to the executable.
- `presentation.json`: inspector metadata contract 1, without source rules.
- `realizations/<id>/realization.json`: version 1 backend envelope.
- `realizations/<id>/geometry.json`: the exact UTF-8 payload bytes emitted by the
  C++ authoring library. Reformatting changes its digest.
- `realizations/<id>/presentation.json`: view/control hints without duplicate mesh data.
- `legacy/`: compatible source package, catalogs, and generated geometry; omitted
  when the project specifies `"includeLegacy": false`.
- `build-manifest.json`: authoring receipt version 1, with project identity,
  SHA-256 input, selected producer, native binary and output hashes, semantic
  identities, and realization inventory. No timestamps or absolute host paths
  enter the receipt.

Generation, export, geometry certification, semantic identity checks, and an
initial runtime frame all succeed before publication. The C++ exporter owns
geometry construction; `twisty_artifact_validate` links the source-free kernel
and consumes the exported execution, control, correspondence, envelope and exact
geometry payload documents. No public kernel API or buffer
version is changed by the authoring project/receipt versions.

`build` reuses an unchanged valid receipt. `--rebuild` regenerates and yields the
same bytes with the same inputs and native binaries. `check` compares provenance,
exact output inventory and bytes, then runs native validation. `check --files-only`
compares hashes without executing native hosts, but still requires their files.
`validate` checks published artifact bytes and native semantics independently of
input freshness. `inspect` reports dependencies and an existing receipt. Check,
validate and inspect never generate, create locks, write bytecode, or alter outputs.
All commands support `--json`; failures report `Invalid`, a code and a message
on stderr with nonzero exit status. Native diagnostics are preserved in messages.

Builds use a sibling staging directory and a single-writer output lock. Inputs
and native tool bytes are hashed again after generation. A failed generator,
exporter or validator leaves the preceding published output intact. Publication
uses rename with exception rollback; there is no cross-directory crash-atomicity
guarantee. A terminated build can leave a lock requiring manual removal after
checking that its recorded process has stopped. Outputs must be dedicated owned
directories; symlinks and unlisted files are rejected before replacement.

## Reference repository compatibility

`npm run build:puzzles` delegates to `tools/authoring/reference-build.mjs`. All
24 models are generated in a private workspace, all 48 realization documents
are validated natively, then schemas, sidecar equality and fixture pins are
checked before publishing. The existing version 1 repository manifest remains
separate from per-project receipts. Publication restores previous generated files
on an exception and updates the global manifest last. Pin updates require the
existing explicit `--update-fixtures` option.

Former `scripts/*.py` model imports and commands forward to
`tools.authoring.models`. Historical `authoringTool` strings remain valid wrapper
paths to preserve existing provenance and semantic identities. The compatibility
commands remain supported, but a project build provides bounded dependencies
and transactional output ownership. Normal web/kernel builds consume prebuilt
packages without invoking generators.

The reference application consumes source-free outputs in `runtime-puzzles/`.
After rebuilding legacy authoring inputs, run `npm run build:runtime-puzzles` to
export all 24 runtime projects and validate all 48 views. This separate command
stages the complete runtime inventory and publishes the global receipt last.
`npm run check:runtime-puzzles` only checks inputs, exact inventory and hashes.
Normal web and SDK builds check freshness and never regenerate puzzle data.
The application fetches only compiled preset data; source import, old-save
conversion and custom Gear authoring load the optional authoring WASM module.
