import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const native = resolve(root, process.argv[2] ?? 'build/native/twisty_language');
const sdk = resolve(root, process.argv[3] ?? 'build/kernel/index.js');
const temporary = await mkdtemp(join(tmpdir(), 'twisty-language-'));
try {
  const oracle = join(temporary, 'oracle.json');
  execFileSync('python3', [join(root, 'tests/language_oracle.py'), native,
    join(root, 'examples/language/coupled.pzl.json'), '--emit', oracle], { stdio: 'inherit' });
  const { createKernel, KernelSession, Puzzle } = await import(pathToFileURL(sdk).href);
  const module = await createKernel();
  const info = JSON.parse(module.kernelInfoJSON());
  assert.equal(info.languageAPIVersion, 3);
  assert.deepEqual(info.authoringLibraries, ['cube3', 'bandaged-uf-ufr', 'gear-family']);
  assert.equal(JSON.parse(module.compilePuzzleJSON('{}')).status, 'Invalid');
  let count = 0;
  for (const suite of JSON.parse(await readFile(oracle, 'utf8'))) {
    const puzzle = new Puzzle(module, suite.source);
    try {
      assert.deepEqual(puzzle.preview, suite.preview, 'Native/WASM compiler output differs.');
      for (let i = 0; i < suite.queries.length; ++i) {
        assert.deepEqual(puzzle.query(suite.queries[i]), suite.expected[i], JSON.stringify(suite.queries[i]));
        ++count;
      }
    } finally { puzzle.dispose(); }
  }
  const source = await readFile(join(root, 'examples/language/coupled.pzl.json'), 'utf8');
  const puzzle = new Puzzle(module, source);
  try {
    assert.equal(puzzle.query({ op: 'bind', state: [0, 2, 4], request: 'synchronize' }).status, 'ChoiceRequired');
    assert.deepEqual(puzzle.query({ op: 'bind', state: [0, 2, 4], request: 'fixed' }),
      { status: 'Selected', choices: ['turn', 'turn', 'idle'] });
    assert.equal(puzzle.query({ op: 'discover', state: [0, 2, 4], request: 'none', candidateLimit: 1, resultLimit: 10 }).complete, false);
    assert.equal(puzzle.query({ op: 'discover', state: [0, 2, 4], request: 'none', candidateLimit: 100, resultLimit: 10 }).complete, true);
    assert.equal(puzzle.query({ op: 'plan', state: [0, 2, 4], request: 'fixed', choices: [] }).status, 'Invalid');
  } finally { puzzle.dispose(); }
  for (const name of ['cube3', 'bandaged', 'helicopter', 'gear', 'gear-extreme', 'gear-plain', 'bagua']) {
    const path = join(root, 'packages', name, 'package.json');
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
      for (const kind of ['finite-definition', 'cube-prototypes', 'gear-cube-prototypes']) {
        const rejected = JSON.parse(module.compileExecutionJSON(JSON.stringify({ kind, schemaVersion: 1 })));
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
  console.log('Production sessions preserve full native/WASM witnesses and replay; retired source formats are rejected.');
  console.log(`Portable language API: ${count} WASM results match the independent source oracle and native consumer.`);
} finally { await rm(temporary, { recursive: true, force: true }); }
