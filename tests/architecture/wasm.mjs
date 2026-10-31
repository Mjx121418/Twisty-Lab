// Each invocation uses a fresh process and the public WASM API only.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { performance } from 'node:perf_hooks';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const root = process.argv[2];
const scenario = JSON.parse(process.argv[3]);
const read = file => readFile(resolve(root, file), 'utf8');
const hash = text => createHash('sha256').update(text).digest('hex');
// Match nlohmann::json's object ordering; raw native JSON strings remain the hash input.
function sorted(value) {
  if (Array.isArray(value)) return value.map(sorted);
  if (value && typeof value === 'object') return Object.fromEntries(Object.keys(value).sort().map(k => [k, sorted(value[k])]));
  return value;
}
const canonical = value => JSON.stringify(sorted(value));
function samples(values) {
  const result = [], stride = Math.max(1, Math.floor(values.length / 32));
  for (let i = 0; i < values.length; i += stride) result.push(values[i]);
  return result;
}
const { default: createModule } = await import(pathToFileURL(resolve(root, 'kernel/generated/twisty.mjs')));
const initStart = performance.now();
const module = await createModule();
const runtimeInitMs = performance.now() - initStart;
const source = await read(scenario.package);
const start = performance.now();
const session = new module.Session(source);
const metrics = { sessionLoadMs: performance.now() - start, runtimeInitMs, packageBytes: Buffer.byteLength(source), geometry: [] };
session.lightweightResults(true);
let controller;
try {
  const definition = session.definitionJSON();
  const observed = { kernel: JSON.parse(module.kernelInfoJSON()), definitionSHA256: hash(definition),
    definitionBytes: Buffer.byteLength(definition), initial: JSON.parse(session.stateSnapshotJSON()) };
  const request = operation => JSON.stringify({ operation, parameters: {} });
  if (scenario.controller) {
    const config = scenario.controller;
    controller = module.createController(session); assert.ok(controller);
    const intent = JSON.stringify(config.intent);
    const plan = JSON.parse(controller.planToJSON(session, intent, session.revision()));
    assert.equal(plan.status, 'Planned'); assert.equal(session.revision(), '0');
    assert.equal(JSON.parse(controller.turnToJSON(session, intent, '999')).status, 'StaleRevision');
    observed.controller = { initial: JSON.parse(controller.familyStateJSON(session, config.family)),
      selection: JSON.parse(controller.familiesForPiecesJSON(session, JSON.stringify(config.pieces))),
      request: plan.request, planSHA256: hash(canonical(plan)) };
  }
  assert.equal(JSON.parse(session.executeJSON(request(scenario.firstOperation), '999')).status, 'StaleRevision');
  assert.equal(session.revision(), '0');
  if (scenario.blockedOperation) {
    const before = session.stateJSON();
    const blocked = JSON.parse(session.executeJSON(request(scenario.blockedOperation), session.revision()));
    assert.equal(blocked.status, 'Blocked');
    assert.equal(session.stateJSON(), before);
    observed.blockedReason = blocked.reasonCode;
  }
  const result = JSON.parse(session.runJSON(scenario.algorithm, 'transactional', session.revision()));
  assert.equal(result.status, 'Committed');
  assert.ok(result.transitions.length);
  observed.after = JSON.parse(session.stateSnapshotJSON());
  if (controller) {
    const config = scenario.controller;
    observed.controller.after = JSON.parse(controller.familyStateJSON(session, config.family));
    observed.controller.selectionAfter = JSON.parse(controller.familiesForPiecesJSON(session, JSON.stringify(config.pieces)));
  }
  observed.witnesses = result.transitions.map(t => ({ request: t.request, sha256: hash(canonical(t)),
    participants: t.pieceActions.length, selfLoops: t.pieceActions.filter(a => a.from === a.to).length }));
  observed.saveSHA256 = hash(session.saveJSON());
  const target = session.stateJSON();
  assert.equal(JSON.parse(session.undoJSON(session.revision())).status, 'Committed');
  const save = session.saveJSON();
  observed.redoTailSaveSHA256 = hash(save);
  const restored = module.createSessionFromDefinition(session);
  assert.ok(restored);
  try {
    restored.lightweightResults(true);
    assert.equal(JSON.parse(restored.loadJSON(save)).status, 'Loaded');
    assert.equal(JSON.parse(restored.redoJSON(restored.revision())).status, 'Committed');
    assert.equal(restored.stateJSON(), target);
  } finally { restored.delete(); }
  assert.equal(JSON.parse(session.redoJSON(session.revision())).status, 'Committed');
  assert.equal(session.stateJSON(), target);
  observed.views = [];
  for (const file of scenario.realizations) {
    const text = await read(file), geometryStart = performance.now();
    const geometry = module.createGeometryDefinition(session, text);
    assert.ok(geometry);
    metrics.geometry.push({ compileMs: performance.now() - geometryStart, realizationBytes: Buffer.byteLength(text) });
    try {
      const scene = JSON.parse(geometry.sceneJSON());
      const view = { id: scene.realizationId, space: JSON.parse(geometry.spaceJSON()), parts: scene.visualParts.length,
        positions: geometry.positions().length, indices: geometry.indices().length, frames: [] };
      for (const transition of result.transitions) {
        const animation = geometry.animate(JSON.stringify(transition));
        const handles = [];
        const evaluate = progress => {
          const frame = animation.evaluate(progress); handles.push(frame);
          // Copy immediately: a later allocation may grow WASM memory.
          return Array.from(frame.isometries());
        };
        try {
          const a = evaluate(0), b = evaluate(1), middle = evaluate(.5);
          assert.ok(middle.every(Number.isFinite), 'Nonfinite intermediate pose.');
          const from = geometry.frame(JSON.stringify(transition.beforeState)); handles.push(from);
          assert.deepEqual(a, Array.from(from.isometries()));
          const to = geometry.frame(JSON.stringify(transition.afterState)); handles.push(to);
          assert.deepEqual(b, Array.from(to.isometries()));
          evaluate(.875);
          assert.deepEqual(middle, evaluate(.5));
          view.frames.push({ durationHint: animation.durationHint(), source: samples(a), middle: samples(middle), target: samples(b) });
        } finally { handles.forEach(h => h.delete()); animation.delete(); }
      }
      const forged = structuredClone(result.transitions[0]); delete forged.request.choices;
      let rejected = false;
      try { const unexpected = geometry.animate(JSON.stringify(forged)); unexpected.delete(); }
      catch { rejected = true; }
      assert.ok(rejected, 'Incomplete witness was accepted.');
      observed.views.push(view);
    } finally { geometry.delete(); }
  }
  metrics.peakRssBytes = process.resourceUsage().maxRSS * 1024;
  process.stdout.write(JSON.stringify({ observed, metrics }) + '\n');
} finally { controller?.delete(); session.delete(); }
