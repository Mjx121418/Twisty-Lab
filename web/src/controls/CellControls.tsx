import { useEffect, useMemo, useState } from 'react';
import { KernelSession, type CellAxisLayout } from '../kernel';
import type { S3FadeSettings } from '../s3Fade';

type Props = { session: KernelSession; revision: string; layout: CellAxisLayout; busy: boolean;
  selectedPieces: readonly string[]; onClearSelection: () => void;
  onMove: (operation: string, revision: string) => void; onFocus: (cell: string) => void;
  onGuide: (cell: string, axis: string) => void; onShell: () => void;
  fade?: S3FadeSettings; onFadeRadius: (radius: number, boundary: 'start' | 'end') => void };

export function CellControls({ session, revision, layout, busy, selectedPieces, onClearSelection, onMove, onFocus, onGuide, onShell, fade, onFadeRadius }: Props) {
  const [cell, setCell] = useState(layout.groups[0].id);
  const [categoryId, setCategory] = useState('face');
  const [axisId, setAxis] = useState('');
  const [matches, setMatches] = useState(layout.groups.map(g => g.id));
  const [matching, setMatching] = useState(false);
  const [legal, setLegal] = useState<string[]>([]);
  const group = layout.groups.find(g => g.id === cell)!;
  const category = group.categories.find(c => c.kind === categoryId)!;
  const axis = category.axes.find(a => a.id === axisId) ?? category.axes[0];
  const selection = JSON.stringify(selectedPieces);
  const turns = useMemo(() => axis.turns, [axis]);
  useEffect(() => {
    if (busy) return;
    onGuide(cell, axis.id);
  }, [cell, axis.id, busy]);
  useEffect(() => {
    let cancelled = false; let timer: ReturnType<typeof setTimeout>;
    if (busy) return;
    if (!selectedPieces.length) { setMatches(layout.groups.map(g => g.id)); setMatching(false); return; }
    setMatching(true); setMatches([]);
    const found: string[] = [];
    const next = (index: number) => {
      if (cancelled || session.native.revision() !== revision) return;
      const g = layout.groups[index];
      if (!g) { setMatches(found); setMatching(false); if (found.length === 1) setCell(found[0]); return; }
      const plan = session.plan({ operation: g.categories[0].axes[0].turns[0].operation, parameters: {} });
      if (plan.status === 'Legal' && selectedPieces.every(p => plan.transition?.pieceActions.some(a => a.pieceId === p))) found.push(g.id);
      timer = setTimeout(() => next(index + 1), 0);
    };
    timer = setTimeout(() => next(0), 0);
    return () => { cancelled = true; clearTimeout(timer); };
  }, [session, revision, selection, busy, layout]);
  useEffect(() => {
    setLegal([]);
    if (busy) return;
    const timer = setTimeout(() => {
      if (session.native.revision() !== revision) return;
      setLegal(turns.filter(t => session.plan({ operation: t.operation, parameters: {} }).status === 'Legal').map(t => t.operation));
    }, 0);
    return () => clearTimeout(timer);
  }, [session, revision, turns, busy]);
  return <section className="family-controls cell-controls" aria-label="Cell twist controller" data-matching-cells={matches.join(',')}>
    <p role="status">{matching ? 'Finding cells…' : selectedPieces.length ? `${selectedPieces.length} pieces selected · ${matches.length} matching cells` : 'Choose a cell, an axis, and a relative turn. Click pieces to find their cells.'}</p>
    {selectedPieces.length > 0 && <button onClick={onClearSelection}>Clear piece selection</button>}
    <div className="cell-groups" role="group" aria-label={layout.groups.length === 8 ? "Eight cells" : "24 cells"}>{layout.groups.map(g =>
      <button key={g.id} aria-pressed={cell === g.id} disabled={busy || matching || !matches.includes(g.id)} onClick={() => setCell(g.id)}>{g.label}</button>)}</div>
    <div className="cell-categories" role="group" aria-label="Axis kind">{group.categories.map(c =>
      <button key={c.kind} aria-pressed={category.kind === c.kind} disabled={busy} onClick={() => setCategory(c.kind)}>{{ face: 'Face axes', edge: 'Edge axes', vertex: 'Vertex axes' }[c.kind]}</button>)}</div>
    <label>Rotation axis <select aria-label="Rotation axis" value={axis.id} disabled={busy} onChange={e => setAxis(e.target.value)}>
      {category.axes.map(a => <option key={a.id} value={a.id}>{a.label}</option>)}</select></label>
    <div className="cell-turns" role="group" aria-label="Relative turns">{turns.map(t => <button key={t.operation} data-operation={t.operation}
      disabled={busy || matching || !matches.includes(cell) || !legal.includes(t.operation)} onClick={() => onMove(t.operation, revision)}>
      {t.angleDegrees > 0 ? '+' : '−'}{Math.abs(t.angleDegrees)}°</button>)}</div>
    <div className="cell-presentation"><button disabled={busy} onClick={() => onFocus(cell)}>Focus {group.label} cell</button>
      <button onClick={onShell}>Toggle outer layers</button></div>
    {fade && <div className="cell-fade">
      <label htmlFor="s3-fade-radius">Fade start radius <output htmlFor="s3-fade-radius">{fade.startRadius.toFixed(2)}</output></label>
      <input id="s3-fade-radius" type="range" min={fade.minRadius} max={fade.maxStartRadius} step="0.01" value={fade.startRadius}
        onChange={e => onFadeRadius(Number(e.target.value), 'start')} aria-describedby="s3-fade-help" />
      <label htmlFor="s3-fade-end-radius">Fade end radius <output htmlFor="s3-fade-end-radius">{fade.endRadius.toFixed(2)}</output></label>
      <input id="s3-fade-end-radius" type="range" min={(fade.minRadius + .01).toFixed(2)} max={fade.maxEndRadius} step="0.01" value={fade.endRadius}
        onChange={e => onFadeRadius(Number(e.target.value), 'end')} aria-describedby="s3-fade-help" />
      <p id="s3-fade-help" className="subtle">Focused pieces stay opaque. Pieces beyond the end radius are hidden. Only the focused cell and its face neighbors are shown.</p>
    </div>}
    <p className="subtle">The gold curve marks the chosen axis. Focus changes the projection; it does not turn the puzzle.</p>
  </section>;
}
