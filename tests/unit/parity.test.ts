import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import createModule, { type MainModule, type Session } from '../../kernel/generated/twisty.mjs';

const read = (path: string): string => readFileSync(path, 'utf8');
let runtime: MainModule;
const handles: Session[] = [];
const session = (path: string): Session => { const value = new runtime.Session(read(path)); handles.push(value); return value; };
const snapshot = (value: Session): any => JSON.parse(value.snapshotJSON());
const native = (args: string[]): any => JSON.parse(execFileSync('build/native/twisty', [...args, '--json'], { encoding: 'utf8' }));

beforeAll(async () => { runtime = await createModule(); });
afterAll(() => { for (const handle of handles) handle.delete(); });

describe('native / WebAssembly parity', () => {
  it.each(['euclidean', 'port-diagram'])('matches native Helicopter assets and sampled %s frames', (kind) => {
    const definition = 'packages/helicopter/definition.json';
    const realization = `packages/helicopter/helicopter-${kind}.json`;
    const algorithm = "UF_ab UL_af (UF_ab UL_af)' UF_ad";
    const reference = JSON.parse(execFileSync('build/native/geometry_probe', [definition, realization, algorithm], { encoding: 'utf8', maxBuffer: 4 * 1024 * 1024 }));
    const value = new runtime.Session(read(definition));
    const geometry = runtime.createGeometry(value, read(realization));
    if (!geometry) throw new Error('Geometry creation failed.');
    const sameFloats = (actual: Float32Array, expected: number[]): void => {
      expect(actual.length).toBe(expected.length);
      for (let i = 0; i < actual.length; i++) expect(Math.abs(actual[i] - expected[i])).toBeLessThan(1e-6);
    };
    try {
      expect(JSON.parse(geometry.sceneJSON())).toEqual(reference.scene);
      sameFloats(geometry.positions() as Float32Array, reference.positions);
      sameFloats(geometry.normals() as Float32Array, reference.normals);
      expect(Array.from(geometry.indices() as Uint32Array)).toEqual(reference.indices);
      sameFloats(geometry.transforms() as Float32Array, reference.initial);
      const result = JSON.parse(value.runJSON(algorithm, 'transactional', '0'));
      expect(result.status).toBe('Committed');
      for (let i = 0; i < result.transitionRecords.length; i++) {
        expect(JSON.parse(geometry.prepareAnimationJSON(result.transitionRecords[i])).status).toBe('Prepared');
        sameFloats(geometry.transforms() as Float32Array, reference.frames[i].source);
        geometry.sample(0.5);
        sameFloats(geometry.transforms() as Float32Array, reference.frames[i].middle);
        geometry.sample(1);
        sameFloats(geometry.transforms() as Float32Array, reference.frames[i].target);
        const endpoint = Array.from(geometry.transforms() as Float32Array);
        geometry.setStateJSON(JSON.stringify(result.transitions[i].afterState));
        expect(Array.from(geometry.transforms() as Float32Array)).toEqual(endpoint);
      }
      expect(snapshot(value).stateDigest).toBe(reference.stateDigest);
    } finally { geometry.delete(); value.delete(); }
  });
  it('keeps a shared geometric definition alive after its session is disposed', () => {
    const value = new runtime.Session(read('packages/helicopter/definition.json'));
    const state = value.stateJSON();
    const geometry = runtime.createGeometry(value, read('packages/helicopter/helicopter-euclidean.json'));
    if (!geometry) throw new Error('Geometry creation failed.');
    value.delete();
    try {
      const initial = Array.from(geometry.transforms() as Float32Array);
      geometry.setStateJSON(state);
      geometry.sample(1);
      expect(Array.from(geometry.transforms() as Float32Array)).toEqual(initial);
      const part = JSON.parse(geometry.sceneJSON()).visualParts[0];
      expect(JSON.parse(geometry.bindHitJSON(part.visualPartId)).pieceId).toBe(part.pieceId);
    } finally { geometry.delete(); }
  });
  it('matches Helicopter jumbling, blocking, inverse and replay records', () => {
    const path = 'packages/helicopter/definition.json';
    const wasm = new runtime.Session(read(path));
    const restored = new runtime.Session(read(path));
    try {
      const algorithm = 'UF_ab DR_ab FR_ad DR_ba UF_ba';
      const result = JSON.parse(wasm.runJSON(algorithm, 'transactional', '0'));
      const reference = native(['run', '--definition', path, '--algorithm', algorithm, '--policy', 'transactional']);
      expect(result.status).toBe('Committed');
      expect(result.snapshot).toEqual(reference.snapshot);
      expect(result.transitions).toEqual(reference.transitions);
      expect(JSON.parse(restored.loadJSON(wasm.saveJSON())).status).toBe('Loaded');
      expect(snapshot(restored).stateDigest).toBe(snapshot(wasm).stateDigest);
      expect(JSON.parse(wasm.runJSON(`(${algorithm})'`, 'transactional', snapshot(wasm).revision)).status).toBe('Committed');
      expect(snapshot(wasm).solved).toBe(true);
      expect(JSON.parse(wasm.runJSON('UF_ab UR_ad', 'transactional', snapshot(wasm).revision)).reasonCode).toBe('placement.blocked');
      expect(snapshot(wasm).solved).toBe(true);
    } finally { wasm.delete(); restored.delete(); }
  });
  it.each(['cube3', 'bandaged'])('matches compilation, state, scramble, and witnesses for %s', (name) => {
    const path = `packages/${name}/source.json`;
    const wasm = session(path);
    const compiled = JSON.parse(runtime.compileJSON(read(path)));
    expect(compiled).toEqual(native(['compile', '--definition', path]));
    expect(snapshot(wasm)).toEqual(native(['inspect', '--definition', path]).snapshot);
    const result = JSON.parse(wasm.scrambleJSON(42, 30, '0'));
    const reference = native(['scramble', '--definition', path, '--seed', '42', '--length', '30']);
    expect(result.snapshot).toEqual(reference.snapshot);
    expect(result.transitions).toEqual(reference.transitions);
    expect(result.scramble).toEqual(reference.scramble);
    const restored = session(path);
    expect(JSON.parse(restored.loadJSON(wasm.saveJSON())).status).toBe('Loaded');
    expect(snapshot(restored).stateDigest).toEqual(snapshot(wasm).stateDigest);
  });

  it('runs the same version-pinned portable fixtures', () => {
    const fixtures = JSON.parse(read('tests/fixtures/core.json'));
    for (const fixture of fixtures.cases) {
      const value = session(fixture.source);
      expect(snapshot(value).definitionDigest).toBe(fixture.definitionDigest);
      expect(JSON.parse(value.runJSON(fixture.prefix, 'transactional', snapshot(value).revision)).status).toBe('Committed');
      const result = JSON.parse(value.executeJSON(JSON.stringify({ operation: fixture.operation, parameters: {} }), snapshot(value).revision));
      expect(result.status, fixture.name).toBe(fixture.expectedStatus);
      if (fixture.reasonCode) expect(result.reasonCode).toBe(fixture.reasonCode);
      if (fixture.implicatedPiece) expect(result.implicatedPieces).toContain(fixture.implicatedPiece);
      if (fixture.unchangedParticipant) expect(result.transitions[0].pieceActions).toContainEqual(expect.objectContaining({ pieceId: fixture.unchangedParticipant, from: 'center:U', to: 'center:U' }));
    }
  });

  it('preserves wide exact integers through native serialization and animation records', () => {
    const compiled = JSON.parse(runtime.compileJSON(read('packages/cube3/source.json'))).definition;
    delete compiled.definitionDigest;
    compiled.initialState.mechanism.counter = 'wide-integer-marker';
    const source = JSON.stringify(compiled).replace('"wide-integer-marker"', '9223372036854775807');
    const value = new runtime.Session(source); handles.push(value);
    const result = JSON.parse(value.runJSON('U', 'interactive', '0'));
    expect(result.transitionRecords[0]).toContain('9223372036854775807');
    const text = value.saveWithPresentationJSON('{"cameraZoom":0.5}');
    expect(text).toContain('"counter":9223372036854775807');
    const restored = new runtime.Session(source); handles.push(restored);
    expect(JSON.parse(restored.loadJSON(text)).status).toBe('Loaded');
    expect(snapshot(restored).stateDigest).toBe(snapshot(value).stateDigest);
    const realization = { ...JSON.parse(read('packages/cube3/cube-euclidean.json')), compatibleDefinitionDigest: snapshot(value).definitionDigest };
    const view = new runtime.Geometry(value.definitionJSON(), JSON.stringify(realization));
    try {
      expect(JSON.parse(view.prepareAnimationJSON(result.transitionRecords[0])).status).toBe('Prepared');
      view.sample(1);
      expect(Array.from(view.transforms() as Float32Array).every(Number.isFinite)).toBe(true);
    } finally { view.delete(); }
  });
});
