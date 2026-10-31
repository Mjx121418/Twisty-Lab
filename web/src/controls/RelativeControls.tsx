import { useEffect, useId, useMemo, useState } from 'react';
import { KernelSession } from '../kernel';

type Props = { session: KernelSession; revision: string; selectedPieces: readonly string[];
  history: readonly string[]; cursor: number;
  busy: boolean; onMove: (operation: string, revision: string) => void; onClearSelection: () => void };

// A freely moving puzzle has no persistent center from which to read an
// absolute phase. Ask the portable planner about relative turns instead.
export function RelativeControls({ session, revision, history, cursor, selectedPieces, busy, onMove, onClearSelection }: Props) {
  const families = useMemo(() => [...new Set(session.definition.operations.map(op => op.family))], [session]);
  const [choice, setChoice] = useState(families[0]);
  const [matches, setMatches] = useState<string[]>(families);
  const [matching, setMatching] = useState(false);
  const [previews, setPreviews] = useState<Record<string, string>>({});
  const [hover, setHover] = useState<string>();
  const markerId = useId();
  const selection = JSON.stringify(selectedPieces);
  const family = matches.includes(choice) ? choice : matches.length === 1 ? matches[0] : '';
  const operations = useMemo(() => session.definition.operations.filter(op => op.family === family)
    .sort((a,b) => angle(a.id)-angle(b.id)), [session, family]);
  function angle(id: string): number { return (session.definition.display?.operations[id]?.angleDegrees?.[0] ?? 0)*Math.PI/180; }
  const operationFamilies = useMemo(() => new Map(session.definition.operations.map(op => [op.id,op.family])), [session]);
  // This puzzle has no fixed center driver. Keep a presentation coordinate
  // from the active recorded moves, so undo, redo and restored sessions agree.
  // It never supplies a phase guard or changes the planner's legal requests.
  const currentAngle = useMemo(() => normalizeAngle(history.slice(0,cursor).reduce((sum,id) =>
    operationFamilies.get(id) === family ? sum+angle(id) : sum,0)), [history, cursor, family, operationFamilies]);
  const currentDegrees = (currentAngle*180/Math.PI).toFixed(3);
  const stops = useMemo(() => {
    const nodes: { id: string; angle: number; radius: number; x: number; y: number }[] = [];
    const turns = [{ id: '', angle: 0 }, ...operations.map(op => ({ id: op.id, angle: angle(op.id) }))]
      .sort((a,b) => (a.angle+2*Math.PI)%(2*Math.PI)-(b.angle+2*Math.PI)%(2*Math.PI));
    for (const turn of turns) {
      const candidates = [120,70,138,94].map(radius => ({ ...turn, radius,
        x: radius*Math.sin(turn.angle), y: -radius*Math.cos(turn.angle) }));
      const spacing = (p: typeof candidates[number]) => Math.min(...nodes.map(q => Math.hypot(p.x-q.x,p.y-q.y)),Infinity);
      nodes.push(candidates.find(p => spacing(p) >= 48) ?? candidates.reduce((a,b) => spacing(a) >= spacing(b) ? a : b));
    }
    return nodes.map(stop => {
      const target = normalizeAngle(currentAngle+stop.angle);
      return { ...stop, angle: target, x: stop.radius*Math.sin(target), y: -stop.radius*Math.cos(target) };
    }).sort((a,b) => a.angle-b.angle);
  }, [operations, currentAngle]);

  useEffect(() => {
    let cancelled = false;
    let timer: ReturnType<typeof setTimeout>;
    if (busy) return;
    if (!selectedPieces.length) { setMatches(families); setMatching(false); return; }
    setMatching(true); setMatches([]);
    const found: string[] = [];
    const next = (index: number) => {
      if (cancelled || session.native.revision() !== revision) return;
      const current = families[index];
      if (!current) { setMatches(found); setMatching(false); return; }
      // The authoring contract identifies the request used to probe this family.
      const plan = session.plan({ operation: session.definition.display?.familyProbes[current] ?? current, parameters: {} });
      if (plan.status === 'Legal' && plan.transition &&
        selectedPieces.every(piece => plan.transition!.pieceActions.some(action => action.pieceId === piece))) found.push(current);
      timer = setTimeout(() => next(index+1),0);
    };
    timer = setTimeout(() => next(0),0);
    return () => { cancelled = true; clearTimeout(timer); };
  }, [session, revision, selection, families, busy]);

  useEffect(() => {
    let cancelled = false;
    let timer: ReturnType<typeof setTimeout>;
    setPreviews({});
    setHover(undefined);
    if (busy || !family) return;
    const next = (index: number) => {
      if (cancelled || session.native.revision() !== revision) return;
      const operation = operations[index];
      if (!operation) return;
      const plan = session.plan({ operation: operation.id, parameters: {} });
      setPreviews(previous => ({ ...previous, [operation.id]: plan.status }));
      timer = setTimeout(() => next(index+1),0);
    };
    timer = setTimeout(() => next(0),0);
    return () => { cancelled = true; clearTimeout(timer); };
  }, [session, revision, family, operations, busy]);

  const delta = hover ? angle(hover) : undefined;
  const arc = delta !== undefined ? `M ${160+120*Math.sin(currentAngle)} ${160-120*Math.cos(currentAngle)} A 120 120 0 ${Math.abs(delta)>Math.PI ? 1 : 0} ${delta>0 ? 1 : 0} ${160+120*Math.sin(currentAngle+delta)} ${160-120*Math.cos(currentAngle+delta)}` : undefined;

  return <section className="family-controls" aria-label="Relative twist controller" data-matching-families={matches.join(',')}>
    <div className="family-piece-selection">
      <p role="status">{matching ? 'Finding available axes…' : selectedPieces.length ?
        `${selectedPieces.length} selected pieces · ${matches.length} available axes include this selection` :
        'Choose an axis and an angular destination. Select pieces to find available axes.'}</p>
      {selectedPieces.length > 0 && <button onClick={onClearSelection}>Clear piece selection</button>}
    </div>
    <label>Twist family <select aria-label="Twist family" value={family} disabled={busy || matching}
      onChange={event => setChoice(event.target.value)}>
      {!family && <option value="">Choose an axis</option>}
      {matches.map(id => <option key={id} value={id}>{id}</option>)}
    </select></label>
    {family && <>
      <div className="stop-rings"><div className="stop-ring relative-ring" role="group" aria-label={`${family} angular stops`} data-current-angle={currentDegrees}>
        <svg viewBox="0 0 320 320" aria-hidden="true">
          <defs><marker id={markerId} viewBox="0 0 10 10" refX="9" refY="5" markerWidth="5" markerHeight="5" orient="auto-start-reverse"><path d="M 0 0 L 10 5 L 0 10 z" fill="#7cdddf" /></marker></defs>
          <circle cx="160" cy="160" r="120" />
          {stops.filter(p => p.radius !== 120).map(p => <line key={p.id}
            x1={160+120*Math.sin(p.angle)} y1={160-120*Math.cos(p.angle)} x2={160+p.x} y2={160+p.y} stroke="#82949e" />)}
          {arc && <path data-testid="relative-turn-arc" d={arc} fill="none" stroke="#7cdddf" strokeWidth="3" markerEnd={`url(#${markerId})`} />}
        </svg>
        <div className="ring-center"><strong>{family}</strong><span aria-live="polite" data-testid={`current-angle-${family}`}>{currentDegrees}°</span></div>
        {stops.map((stop,index) => {
          const current = !stop.id;
          const status = current ? 'Current' : previews[stop.id] ?? 'Pending';
          const degrees = stop.angle*180/Math.PI;
          return <button key={stop.id} type="button" data-stop={stop.id || 'current'} data-angle={stop.angle} data-status={status}
            className={`angular-stop relative-stop ${current ? 'current-stop' : ''} ${status === 'Blocked' ? 'blocked-stop' : ''} ${hover === stop.id ? 'draft-stop' : ''}`}
            style={{ left: `${(160+stop.x)/3.2}%`, top: `${(160+stop.y)/3.2}%` }} disabled={busy || (!current && status === 'Pending')}
            aria-label={current ? `${family} current position, ${currentDegrees} degrees` : `${family} turn ${angle(stop.id) >= 0 ? '+' : ''}${(angle(stop.id)*180/Math.PI).toFixed(3)} degrees`}
            aria-current={current ? 'step' : undefined}
            title={current ? `Current axis position · ${currentDegrees}°` : `${degrees.toFixed(3)}° destination · ${stop.id} · ${status}`}
            onMouseEnter={() => setHover(stop.id || undefined)} onMouseLeave={() => setHover(undefined)}
            onFocus={() => setHover(stop.id || undefined)} onBlur={() => setHover(undefined)}
            onKeyDown={event => {
              if (!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(event.key)) return;
              event.preventDefault(); event.stopPropagation();
              const buttons = Array.from(event.currentTarget.parentElement!.querySelectorAll<HTMLButtonElement>('button[data-stop]'));
              const direction = ['ArrowLeft','ArrowUp'].includes(event.key) ? -1 : 1;
              for (let offset=1; offset<buttons.length; offset++) {
                const next = buttons[(index+direction*offset+buttons.length)%buttons.length];
                if (!next.disabled) { next.focus(); break; }
              }
            }}
            onClick={() => { if (!current) onMove(stop.id,revision); }}>
            {current ? `${Number(currentDegrees).toFixed(1)}°` : `${degrees.toFixed(1)}°`}
          </button>;
        })}
      </div></div>
      <p className="subtle" aria-live="polite">{delta === undefined ? 'Gold marks the current axis position, measured from the session’s starting position. Choose an angular destination.' :
        `${currentDegrees}° → ${(normalizeAngle(currentAngle+delta)*180/Math.PI).toFixed(3)}° (${delta > 0 ? '+' : ''}${(delta*180/Math.PI).toFixed(3)}°) · ${previews[hover!] ?? 'Checking…'}`}</p>
      <details><summary>Turn list</summary><label>{family} destination <select aria-label={`${family} destination list`} value="" disabled={busy}
        onChange={event => { if (event.target.value) onMove(event.target.value,revision); }}>
        <option value="">Choose an angular destination</option>
        {operations.map(op => <option key={op.id} value={op.id} disabled={!previews[op.id]}>
          {(normalizeAngle(currentAngle+angle(op.id))*180/Math.PI).toFixed(3)}° ({angle(op.id)>0 ? '+' : ''}{(angle(op.id)*180/Math.PI).toFixed(3)}°) · {previews[op.id] ?? 'Checking…'}
        </option>)}
      </select></label></details>
    </>}
  </section>;
}

function normalizeAngle(angle: number): number {
  const fullTurn = 2*Math.PI;
  const value = (angle%fullTurn+fullTurn)%fullTurn;
  return Math.min(value,fullTurn-value) < 1e-8 ? 0 : value;
}
