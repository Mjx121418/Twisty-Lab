import { readFileSync } from 'node:fs';
import { beforeAll, describe, expect, it, vi } from 'vitest';
import { createRuntime, GeometryInstance, RuntimeSession as KernelSession, type MainModule } from '../../kernel/runtime';
import { destinationTransforms, pieceDestinations } from '../../web/src/destinations';

const read = (path: string): string => readFileSync(path, 'utf8');
const sessionFor = (id: string) => new KernelSession(runtime, {
  abstract:read(`runtime-puzzles/${id}/abstract.json`),controls:read(`runtime-puzzles/${id}/controls.json`),presentation:read(`runtime-puzzles/${id}/presentation.json`),
});
const geometryFor = (session: KernelSession,id:string,view:string) => new GeometryInstance(runtime,session.native,{
  interpretation:read(`runtime-puzzles/${id}/interpretation.json`),
  envelope:read(`runtime-puzzles/${id}/realizations/${view}/realization.json`),payload:read(`runtime-puzzles/${id}/realizations/${view}/geometry.json`),
});
let runtime: MainModule;
beforeAll(async () => { runtime = await createRuntime(); });

describe('selected-piece destinations', () => {
  it('shares verified plans across selections and refreshes them at each revision', () => {
    const session = sessionFor('cube3');
    const planner = vi.spyOn(session, 'plan');
    try {
      const initial = session.snapshot();
      const corner = pieceDestinations(session, initial, 'corner/UFR');
      expect(corner).toHaveLength(6);
      expect(planner).toHaveBeenCalledTimes(initial.legalRequests.length);
      expect(pieceDestinations(session, initial, 'center/U').map(option => option.request.operation)).toEqual(['U', "U'"]);
      corner.length = 0;
      expect(pieceDestinations(session, initial, 'corner/UFR')).toHaveLength(6);
      expect(planner).toHaveBeenCalledTimes(initial.legalRequests.length);
      session.move('F');
      expect(pieceDestinations(session, initial, 'corner/UFR')).toEqual([]);
      const next = session.snapshot();
      expect(pieceDestinations(session, next, 'corner/UFR').every(option => option.revision === next.revision)).toBe(true);
      expect(planner).toHaveBeenCalledTimes(initial.legalRequests.length + next.legalRequests.length);
      session.undo();
      const restored = session.snapshot();
      expect(pieceDestinations(session, restored, 'corner/UFR').map(option => option.request.operation)).toEqual(['F', "F'", 'R', "R'", 'U', "U'"]);
      expect(planner).toHaveBeenCalledTimes(initial.legalRequests.length + next.legalRequests.length + restored.legalRequests.length);
    } finally { session.dispose(); }
  });

  it('reuses verified endpoint frames while preserving buffer ownership and rejecting altered records', () => {
    const session = sessionFor('cube3');
    const geometry = geometryFor(session,'cube3','cube-euclidean');
    const animate = vi.spyOn(geometry.definition, 'animate');
    try {
      const option = pieceDestinations(session, session.snapshot(), 'corner/UFR')[0];
      const live = Array.from(geometry.matrices);
      const first = geometry.destination(option.transitionRecord);
      const expected = Array.from(first);
      first.fill(Number.NaN);
      const second = geometry.destination(option.transitionRecord);
      expect(Array.from(second)).toEqual(expected);
      expect(second).not.toBe(first);
      expect(animate).toHaveBeenCalledTimes(1);
      expect(Array.from(destinationTransforms(geometry, option))).toEqual(expected);
      expect(animate).toHaveBeenCalledTimes(1);
      expect(() => destinationTransforms(geometry, { ...option, executionDigest: 'wrong' })).toThrow();
      expect(() => destinationTransforms(geometry, { ...option, targetStateText: '{}' })).toThrow();
      expect(Array.from(geometry.matrices)).toEqual(live);
      const corrupt = JSON.parse(option.transitionRecord);
      corrupt.request.choices[0] = 'idle';
      expect(() => geometry.destination(JSON.stringify(corrupt))).toThrow();
      expect(animate).toHaveBeenCalledTimes(2);
      expect(Array.from(geometry.destination(option.transitionRecord))).toEqual(expected);
    } finally { geometry.dispose(); session.dispose(); }
  });
  it.each([
    ['cube3', 'package.json', 'corner/UFR', ['F', "F'", 'R', "R'", 'U', "U'"]],
    ['bandaged', 'package.json', 'bandage/UF-UFR', ['F', "F'", 'U', "U'"]],
    ['helicopter', 'package.json', 'corner/BDL', ['BL_ab', 'BL_ac', 'BL_ad', 'BL_ae', 'BL_af', 'DB_ab', 'DB_ac', 'DB_ad', 'DB_ae', 'DB_af', 'DL_ab', 'DL_ac', 'DL_ad', 'DL_ae', 'DL_af']],
  ])('plans only legal participating moves for %s', (puzzle, source, pieceId, expected) => {
    const session = sessionFor(puzzle);
    try {
      const before = session.save();
      const snapshot = session.snapshot();
      const options = pieceDestinations(session, snapshot, pieceId as string);
      expect(options.map((option) => option.request.operation).sort()).toEqual(expected);
      expect(options.every((option) => option.revision === snapshot.revision && option.executionDigest === snapshot.executionDigest)).toBe(true);
      expect(session.save()).toBe(before);
      expect(pieceDestinations(session, snapshot, 'missing-piece')).toEqual([]);
    } finally { session.dispose(); }
  });

  it('includes centers that participate without changing placement and rejects old source revisions', () => {
    const session = sessionFor('cube3');
    try {
      const snapshot = session.snapshot();
      const options = pieceDestinations(session, snapshot, 'center/U');
      expect(options.map((option) => option.request.operation)).toEqual(['U', "U'"]);
      expect(JSON.parse(options[0].transitionRecord).pieceActions).toContainEqual(expect.objectContaining({ pieceId: 'center/U', from: 'center/center:U', to: 'center/center:U' }));
      expect(session.move('F').status).toBe('Committed');
      const newer = session.save();
      expect(pieceDestinations(session, snapshot, 'center/U')).toEqual([]);
      expect(session.execute(options[0].request, options[0].revision).status).toBe('StaleRevision');
      expect(session.save()).toBe(newer);
    } finally { session.dispose(); }
  });

  it('preserves exact records and previews destinations without changing the session or live geometry', () => {
    const session = sessionFor('cube3');
    const live = geometryFor(session,'cube3','cube-spherical');
    const preview = geometryFor(session,'cube3','cube-spherical');
    try {
      const before = session.save();
      const liveFrame = Array.from(live.matrices);
      const options = pieceDestinations(session, session.snapshot(), 'corner/UFR');
      for (const option of options) {
        expect(JSON.parse(option.transitionRecord).request.choices).toHaveLength(3);
        expect(preview.prepare(option.transitionRecord)).toBeGreaterThan(0);
        preview.evaluate(1);
        expect(Array.from(preview.matrices).every(Number.isFinite)).toBe(true);
      }
      expect(Array.from(live.matrices)).toEqual(liveFrame);
      expect(session.save()).toBe(before);
    } finally { preview.dispose(); live.dispose(); session.dispose(); }
  });
});
