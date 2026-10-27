import { readFileSync } from 'node:fs';
import { beforeAll, expect, it } from 'vitest';
import { createKernel, GeometryInstance, KernelSession, type MainModule } from '../../kernel/index';
let module: MainModule;
beforeAll(async () => { module = await createKernel(); });
const read = (file: string) => readFileSync(`packages/tesseract3/${file}`, 'utf8');
it('owns deformed buffers, reprojects without changing state, and keeps seeking after a rejected option', () => {
  const session = new KernelSession(module, read('package.json'));
  session.lightweightResults();
  const geometry = new GeometryInstance(module, session.native, read('tesseract3-s3.json'));
  try {
    expect(geometry.scene.space.homogeneous).toBe(false);
    expect(geometry.scene.space.pointCoordinates).toBe(4);
    expect(geometry.scene.visualParts).toHaveLength(216);
    const original = geometry.positions.slice();
    const result = session.move('Xp_fY');
    const record = result.transitionRecords![0];
    const state = session.stateSnapshot();
    geometry.prepare(record); geometry.evaluate(.37);
    expect(geometry.positions).not.toEqual(original);
    const live = geometry.positions.slice();
    geometry.setProjection({ focusCell: 'Xp', guide: { group: 'Xp', axis: 'fY' } });
    expect(geometry.scene.guidePieces).toHaveLength(27);
    expect(session.stateSnapshot()).toEqual(state);
    const focused = geometry.positions.slice();
    expect(() => geometry.setProjection({ clipRadius: 0 })).toThrow();
    expect(geometry.positions).toEqual(focused);
    expect(geometry.projectionOptions.focusCell).toBe('Xp');
    geometry.evaluate(.7); geometry.evaluate(.37);
    expect(geometry.positions).toEqual(focused);
    geometry.setProjection({});
    expect(geometry.scene.axisGuide).toBeUndefined();
    expect(geometry.positions).toEqual(live);
    expect(() => geometry.destination(record)).toThrow(/Dynamic destinations/);
    geometry.evaluate(1);
    geometry.setState(session.stateText());
    const after = geometry.positions.slice();
    const saved = session.save({ projection: geometry.projectionOptions });
    session.undo(); session.redo();
    expect(session.stateSnapshot().stateDigest).toBe(state.stateDigest);
    expect(session.load(saved).status).toBe('Loaded');
    geometry.setState(session.stateText()); expect(geometry.positions).toEqual(after);
  } finally { geometry.dispose(); session.dispose(); }
});

it('keeps projected drawing storage alive after deleting its producers', () => {
  const session = new KernelSession(module, read('package.json'));
  const definition = module.createGeometryDefinition(session.native, read('tesseract3-s3.json'))!;
  const frame = definition.frame(session.stateText());
  const drawing = definition.projectWithOptions(frame, 'stereographic', JSON.stringify({ focusCell: 'Zp' }));
  const expected = Array.from(drawing.positions() as Float32Array);
  frame.delete(); definition.delete(); session.dispose();
  try { expect(Array.from(drawing.positions() as Float32Array)).toEqual(expected); }
  finally { drawing.delete(); }
});

it('rejects malformed S³ document shapes at the schema boundary', async () => {
  const { default: Ajv } = await import('ajv');
  const valid = new Ajv({ strict: false }).compile(JSON.parse(readFileSync('schemas/realization.schema.json', 'utf8')));
  const original = JSON.parse(read('tesseract3-s3.json'));
  expect(valid(original)).toBe(true);
  for (const alter of [
    (r: typeof original) => r.assets['1/0'].vertices[0].pop(),
    (r: typeof original) => r.operationTracks.Xp_fY.planeU.pop(),
    (r: typeof original) => { r.controlLayout.version = 1; },
    (r: typeof original) => { r.controlLayout.groups[0].categories[0].kind = 'unknown'; },
  ]) {
    const bad = structuredClone(original); alter(bad); expect(valid(bad)).toBe(false);
  }
});
