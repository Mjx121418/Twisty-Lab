import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const native = resolve(root, process.argv[2] ?? 'build/native/twisty_relational');
const sdk = resolve(root, process.argv[3] ?? 'build/kernel/index.js');
const temporary = await mkdtemp(join(tmpdir(), 'twisty-relational-'));
try {
  const oracle = join(temporary, 'oracle.json');
  execFileSync('python3', [join(root, 'tests/relational_oracle.py'), native,
    join(root, 'examples/relational/coupled.pzl.json'), '--emit', oracle], { stdio: 'inherit' });
  const migrationCases = join(temporary, 'cube-migration.json');
  execFileSync(join(dirname(native), 'cube_relational_tests'), ['--emit', migrationCases], { stdio: 'inherit' });
  const helicopterCases = join(temporary, 'helicopter-migration.json');
  execFileSync(join(dirname(native), 'helicopter_relational_tests'), ['--emit', helicopterCases], { stdio: 'inherit' });
  const gearCases = join(temporary, 'gear-migration.json');
  execFileSync(join(dirname(native), 'gear_relational_tests'), ['--emit', gearCases], { stdio: 'inherit' });
  const baguaCases = join(temporary, 'bagua-migration.json');
  execFileSync(join(dirname(native), 'bagua_relational_tests'), ['--emit', baguaCases], { stdio: 'inherit' });
  const { createKernel, KernelSession, ExperimentalRelationalPuzzle, createRelationalCubeSource, createRelationalGearSource } = await import(pathToFileURL(sdk).href);
  const module = await createKernel();
  const info = JSON.parse(module.kernelInfoJSON());
  assert.equal(info.experimentalAPIs.relational, 1);
  assert.deepEqual(info.relationalLibraries, ['cube3', 'bandaged-uf-ufr', 'gear-family']);
  assert.equal(JSON.parse(module.compileRelationalJSON('{}')).status, 'Invalid');
  let count = 0;
  for (const suite of JSON.parse(await readFile(oracle, 'utf8'))) {
    const puzzle = new ExperimentalRelationalPuzzle(module, suite.source);
    try {
      assert.deepEqual(puzzle.preview, suite.preview, 'Native/WASM compiler output differs.');
      for (let i = 0; i < suite.queries.length; ++i) {
        assert.deepEqual(puzzle.query(suite.queries[i]), suite.expected[i], JSON.stringify(suite.queries[i]));
        ++count;
      }
    } finally { puzzle.dispose(); }
  }
  let migrationCount = 0;
  for (const suite of JSON.parse(await readFile(migrationCases, 'utf8'))) {
    const bandaged = suite.catalog.puzzleId === 'bandaged-uf-ufr';
    assert.deepEqual(createRelationalCubeSource(module, bandaged), { source: JSON.parse(suite.source), catalog: suite.catalog });
    const puzzle = new ExperimentalRelationalPuzzle(module, suite.source);
    try {
      assert.deepEqual(puzzle.preview, suite.preview);
      for (let i = 0; i < suite.queries.length; ++i) {
        assert.deepEqual(puzzle.query(suite.queries[i]), suite.expected[i], JSON.stringify(suite.queries[i]));
        ++migrationCount;
      }
    } finally { puzzle.dispose(); }
  }
  const helicopter = JSON.parse(await readFile(helicopterCases, 'utf8'));
  const helicopterPuzzle = new ExperimentalRelationalPuzzle(module, helicopter.source);
  let helicopterCount = 0;
  try {
    assert.deepEqual(helicopterPuzzle.preview, helicopter.preview);
    const foreignAxis = [...helicopter.preview.program.initialPlacements];
    foreignAxis[helicopter.preview.bindings.pieces.indexOf('mechanism/UF')] =
      helicopter.preview.bindings.placements.indexOf('mechanism/edge/UR/a');
    assert.deepEqual(helicopterPuzzle.query({ op: 'validate', state: foreignAxis }), { status: 'Invalid' });
    for (let i = 0; i < helicopter.queries.length; ++i) {
      assert.deepEqual(helicopterPuzzle.query(helicopter.queries[i]), helicopter.expected[i], JSON.stringify(helicopter.queries[i]));
      ++helicopterCount;
    }
  } finally { helicopterPuzzle.dispose(); }
  let gearCount = 0;
  for (const suite of JSON.parse(await readFile(gearCases, 'utf8'))) {
    assert.deepEqual(createRelationalGearSource(module, JSON.parse(suite.wrapper)), { source: JSON.parse(suite.source), catalog: suite.catalog });
    const puzzle = new ExperimentalRelationalPuzzle(module, suite.source);
    try {
      assert.deepEqual(puzzle.preview, suite.preview);
      for (let i = 0; i < suite.queries.length; ++i) {
        assert.deepEqual(puzzle.query(suite.queries[i]), suite.expected[i], JSON.stringify(suite.queries[i]));
        ++gearCount;
      }
    } finally { puzzle.dispose(); }
  }
  const bagua = JSON.parse(await readFile(baguaCases, 'utf8'));
  const baguaPuzzle = new ExperimentalRelationalPuzzle(module, bagua.source);
  try {
    assert.deepEqual(baguaPuzzle.preview, bagua.preview);
    for (let i = 0; i < bagua.queries.length; ++i)
      assert.deepEqual(baguaPuzzle.query(bagua.queries[i]), bagua.expected[i], JSON.stringify(bagua.queries[i]));
  } finally { baguaPuzzle.dispose(); }
  const source = await readFile(join(root, 'examples/relational/coupled.pzl.json'), 'utf8');
  const puzzle = new ExperimentalRelationalPuzzle(module, source);
  try {
    assert.equal(puzzle.query({ op: 'bind', state: [0, 2, 4], request: 'synchronize' }).status, 'ChoiceRequired');
    assert.deepEqual(puzzle.query({ op: 'bind', state: [0, 2, 4], request: 'fixed' }),
      { status: 'Selected', choices: ['turn', 'turn', 'idle'] });
    assert.equal(puzzle.query({ op: 'discover', state: [0, 2, 4], request: 'none', candidateLimit: 1, resultLimit: 10 }).complete, false);
    assert.equal(puzzle.query({ op: 'discover', state: [0, 2, 4], request: 'none', candidateLimit: 100, resultLimit: 10 }).complete, true);
    assert.equal(puzzle.query({ op: 'plan', state: [0, 2, 4], request: 'fixed', choices: [] }).status, 'Invalid');
  } finally { puzzle.dispose(); }
  for (const name of ['cube3', 'bandaged', 'helicopter', 'gear', 'gear-extreme', 'gear-plain', 'bagua']) {
    const path = join(root, 'packages', name, 'relational-package.json');
    const text = await readFile(path, 'utf8');
    const session = new KernelSession(module, text);
    const restored = new KernelSession(module, session);
    let restoredDisposed = false;
    try {
      const algorithm = name === 'bagua' ? "U+ R' L' D2 R L U-" : name === 'helicopter' ? 'UF_ab UL_af' : name === 'gear' ? "R U F (R U F)'" : name === 'gear-extreme' ? "Uq R Fq (Uq R Fq)'" : name === 'gear-plain' ? "Rq Uq Fq (Rq Uq Fq)'" : "U R R' U'";
      const reference = JSON.parse(execFileSync(join(dirname(native), 'twisty'), ['run', '--definition', path,
        '--algorithm', algorithm, '--policy', 'transactional', '--json'], { encoding: 'utf8' }));
      const result = session.run(algorithm, 'transactional');
      assert.equal(result.status, 'Committed');
      assert.deepEqual(result.snapshot, reference.snapshot);
      assert.deepEqual(result.transitions, reference.transitions);
      assert.equal(session.definition.kind, 'relational-definition');
      const saved = JSON.parse(session.save());
      assert.equal(saved.schemaVersion, 2);
      assert.ok(saved.logical.requests.every((r) => Array.isArray(r.choices)));
      assert.equal(restored.snapshot().revision, '0', 'Sharing a definition must start a fresh history.');
      assert.equal(restored.load(JSON.stringify(saved)).status, 'Loaded');
      assert.equal(restored.snapshot().stateDigest, session.snapshot().stateDigest);
      assert.equal(session.undo().status, 'Committed');
      assert.equal(session.redo().status, 'Committed');
      assert.equal(session.snapshot().stateDigest, reference.snapshot.stateDigest);
      const before = restored.snapshot();
      delete saved.logical.requests[0].choices;
      assert.equal(restored.load(JSON.stringify(saved)).status, 'Invalid');
      assert.deepEqual(restored.snapshot(), before);
      restored.dispose();
      restoredDisposed = true;
      for (const path of ['definition.json', ...(name.startsWith('gear') || name === 'cube3' || name === 'bandaged' ? ['source.json'] : [])]) {
        const rejected = JSON.parse(module.compileExecutionJSON(await readFile(join(root, 'packages', name, path), 'utf8')));
        assert.equal(rejected.status, 'Invalid');
        assert.equal(rejected.diagnostics[0].reasonCode, 'definition.unsupported');
      }
    } finally { session.dispose(); if (!restoredDisposed) restored.dispose(); }
    const reloaded = new KernelSession(module, session.definitionText);
    try {
      assert.equal(reloaded.definitionText, session.definitionText, 'Persisted definition bytes changed on reload.');
      assert.equal(reloaded.snapshot().solved, true);
    } finally { reloaded.dispose(); }
    if (name === 'bagua') {
      const compiled = JSON.parse(module.compileExecutionJSON(text));
      assert.equal(compiled.status, 'Compiled', JSON.stringify(compiled.diagnostics));
      assert.deepEqual(compiled.definition, session.definition);
    }
  }
  console.log(`Gear migration: ${gearCount} WASM results preserve coupling, phases and per-domain event witnesses.`);
  console.log(`Bagua migration: ${bagua.queries.length} WASM results preserve occupancy, blocking, movements and witnesses.`);
  console.log('All production presets preserve full native/WASM witnesses and replay; retired source formats are rejected.');
  console.log(`Portable relational API: ${count} WASM results match the independent source oracle and native consumer.`);
  console.log(`Helicopter migration: ${helicopterCount} WASM results match the previous executor's phases, states, legality and full witnesses.`);
  console.log(`Cube/bandage migration: ${migrationCount} WASM results match the current executor's states, legality and full event witnesses.`);
} finally { await rm(temporary, { recursive: true, force: true }); }
