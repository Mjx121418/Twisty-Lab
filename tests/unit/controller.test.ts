import { readFileSync } from 'node:fs';
import { beforeAll, expect, it } from 'vitest';
import { createKernel, KernelController, KernelSession, GeometryInstance, type MainModule } from '../../kernel/index';

let module: MainModule;
beforeAll(async () => { module = await createKernel(); });
const read = (name: string) => readFileSync(name, 'utf8');

it('exposes portable destination queries and lightweight revision-checked execution', () => {
  const session = new KernelSession(module, read('packages/helicopter/package.json'));
  const control = new KernelController(module, session);
  try {
    expect(control.catalog().families).toHaveLength(12);
    expect(control.familyState('UF').current).toEqual({ UF: 'a' });
    const intent = { family: 'UF', targets: { UF: 'd' } };
    const before = session.native.saveJSON();
    expect(control.planTo(intent).status).toBe('Planned');
    expect(session.native.saveJSON()).toBe(before);
    const result = control.turnTo(intent, '0');
    expect(result.status).toBe('Committed');
    expect(result.snapshot).not.toHaveProperty('legalRequests');
    expect(result.transitionRecords).toHaveLength(1);
    expect(control.familyState('UF').current).toEqual({ UF: 'd' });
    expect(control.turnTo(intent, '0').status).toBe('StaleRevision');
    expect(control.turnTo(intent).status).toBe('NoOp');
    const page = control.destinations('UF', 2, 2);
    expect(page.destinations).toHaveLength(2);
    expect(page.nextOffset).toBe(4);
    expect(page.destinations![0]).not.toHaveProperty('transition');
    expect(control.destinations('UF', 0, 100).status).toBe('Invalid');
    session.lightweightResults();
    expect(session.undo().snapshot).not.toHaveProperty('candidateRequests');
    expect(control.familyState('UF').current).toEqual({ UF: 'a' });
    expect(session.snapshot().candidateRequests).toHaveLength(60);
    const geometry = new GeometryInstance(module, session.native, read('packages/helicopter/helicopter-euclidean.json'));
    try {
      expect(geometry.scene.controlLayout?.drivers.UF.angles).toHaveLength(6);
      const record = control.turnTo(intent).transitionRecords![0];
      geometry.prepare(record); geometry.evaluate(.5); geometry.evaluate(1);
      expect(Array.from(geometry.matrices).every(Number.isFinite)).toBe(true);
    } finally { geometry.dispose(); }
  } finally { control.dispose(); session.dispose(); }
});

it('keeps unannotated packages usable through existing request controls', () => {
  const session = new KernelSession(module, read('packages/cube3/package.json'));
  const control = new KernelController(module, session);
  try {
    expect(control.catalog().families).toEqual([]);
    expect(control.familyState('R').status).toBe('Unavailable');
    expect(session.move('R').status).toBe('Committed');
  } finally { control.dispose(); session.dispose(); }
});
