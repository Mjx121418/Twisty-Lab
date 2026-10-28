import { readFileSync } from 'node:fs';
import { beforeAll, describe, expect, it } from 'vitest';
import { createKernel, GeometryInstance, KernelSession, type MainModule } from '../../kernel/index';

const read = (path: string): string => readFileSync(path, 'utf8');
let module: MainModule;
beforeAll(async () => { module = await createKernel(); });

describe('explicit geometric frames', () => {
  it('keeps animations, frames, and drawings alive independently of their source handles', () => {
    const session = new KernelSession(module, read('packages/cube3/package.json'));
    const definition = module.createGeometryDefinition(session.native, read('packages/cube3/cube-euclidean.json'))!;
    const state = session.stateText();
    const record = session.move('U').transitionRecords![0];
    const first = definition.animate(record), second = definition.animate(record);
    const source = definition.frame(state), quarter = first.evaluate(.25);
    const expected = Array.from(quarter.isometries() as Float64Array);
    const drawing = definition.project(quarter, 'embedding');
    const scene = JSON.parse(drawing.sceneJSON());
    const center = scene.visualParts.findIndex((p: { visualPartId: string }) => p.visualPartId === 'center/U/port/0');
    expect(center).toBeGreaterThanOrEqual(0);
    expect(expected.slice(center * 16, center * 16 + 16)).not.toEqual(Array.from(source.isometries() as Float64Array).slice(center * 16, center * 16 + 16));
    const endpoint = second.evaluate(1); endpoint.delete();
    const again = first.evaluate(.25);
    expect(Array.from(again.isometries() as Float64Array)).toEqual(expected); again.delete();
    const invalid = JSON.parse(record); delete invalid.request.choices;
    expect(() => definition.animate(JSON.stringify(invalid))).toThrow();
    expect(() => first.evaluate(NaN)).toThrow();
    expect(() => definition.project(quarter, 'poincare-ball')).toThrow();
    expect(() => definition.intrinsicMesh()).toThrow();
    session.dispose(); definition.delete(); source.delete(); second.delete();
    try {
      const frame = first.evaluate(.25);
      try { expect(Array.from(frame.isometries() as Float64Array)).toEqual(expected); }
      finally { frame.delete(); }
      expect(Array.from(quarter.isometries() as Float64Array)).toEqual(expected);
      expect((drawing.positions() as Float32Array).length).toBeGreaterThan(0);
      expect(JSON.parse(quarter.spaceJSON()).isometryScalar).toBe('float64');
    } finally { first.delete(); quarter.delete(); drawing.delete(); }
  });

  it('shares assets across independent WASM compatibility instances', () => {
    const session = new KernelSession(module, read('packages/cube3/package.json'));
    const definition = module.createGeometryDefinition(session.native, read('packages/cube3/cube-euclidean.json'))!;
    const first = module.createGeometryInstance(definition)!, second = module.createGeometryInstance(definition)!;
    const initial = Array.from(second.transforms() as Float32Array);
    const firstOffset = (first.positions() as Float32Array).byteOffset;
    expect((second.positions() as Float32Array).byteOffset).toBe(firstOffset);
    const record = session.move('U').transitionRecords![0];
    definition.delete(); session.dispose();
    try {
      expect(JSON.parse(first.prepareAnimationJSON(record)).status).toBe('Prepared');
      first.sample(.5);
      expect(Array.from(first.transforms() as Float32Array)).not.toEqual(initial);
      expect(Array.from(second.transforms() as Float32Array)).toEqual(initial);
    } finally { first.delete(); }
    try {
      expect((second.positions() as Float32Array).length).toBeGreaterThan(0);
      second.sample(1);
      expect(Array.from(second.transforms() as Float32Array)).toEqual(initial);
    } finally { second.delete(); }
  });

  it('publishes display scale separately from intrinsic isometries', () => {
    const session = new KernelSession(module, read('packages/helicopter/package.json'));
    const realization = read('packages/helicopter/helicopter-euclidean.json');
    const definition = module.createGeometryDefinition(session.native, realization)!;
    const frame = definition.frame(session.stateText());
    const drawing = definition.project(frame, 'embedding');
    try {
      const scale = JSON.parse(realization).scale;
      expect(scale).not.toBe(1);
      expect(JSON.parse(definition.sceneJSON()).visualParts[0].meshScale).toBe(scale);
      expect(Math.hypot(...(frame.isometries() as Float64Array).slice(0, 3))).toBeCloseTo(1, 10);
      expect(Math.hypot(...(drawing.transforms() as Float32Array).slice(0, 3))).toBeCloseTo(scale, 5);
    } finally { drawing.delete(); frame.delete(); definition.delete(); session.dispose(); }
  });

  it('preserves a full signed 300-degree gear path with linear progress', () => {
    const session = new KernelSession(module, read('packages/gear/package.json'));
    const geometry = new GeometryInstance(module, session.native, read('packages/gear/gear-normal-euclidean.json'));
    try {
      const index = geometry.scene.visualParts.findIndex(p => p.visualPartId === 'edge/UR/cog');
      expect(index).toBeGreaterThanOrEqual(0);
      const initial = Array.from(geometry.matrices.slice(index * 16, index * 16 + 16));
      const record = session.move('F').transitionRecords![0];
      geometry.prepare(record); geometry.evaluate(.25);
      const t = 5 * Math.PI / 3 * .25, axis = [1 / Math.sqrt(2), 1 / Math.sqrt(2), 0];
      for (let c = 0; c < 4; ++c) {
        const v = initial.slice(c * 4, c * 4 + 3);
        const cross = [axis[1] * v[2], -axis[0] * v[2], axis[0] * v[1] - axis[1] * v[0]];
        const dot = axis.reduce((sum, a, i) => sum + a * v[i], 0);
        for (let r = 0; r < 3; ++r)
          expect(geometry.matrices[index * 16 + c * 4 + r]).toBeCloseTo(v[r] * Math.cos(t) + cross[r] * Math.sin(t) + axis[r] * dot * (1 - Math.cos(t)), 5);
      }
      const live = Array.from(geometry.matrices), snapshot = session.snapshot();
      const broken = JSON.parse(record); delete broken.request.choices;
      expect(() => geometry.prepare(JSON.stringify(broken))).toThrow();
      expect(() => geometry.setState('{}')).toThrow();
      expect(Array.from(geometry.matrices)).toEqual(live);
      geometry.evaluate(.75); geometry.evaluate(.25);
      expect(Array.from(geometry.matrices)).toEqual(live);
      geometry.destination(record);
      expect(Array.from(geometry.matrices)).toEqual(live);
      expect(session.snapshot()).toEqual(snapshot);
      expect(geometry.scene.space.intrinsicDimension).toBe(3);
    } finally { geometry.dispose(); session.dispose(); }
  });
});
