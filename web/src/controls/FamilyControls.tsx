import { useEffect, useMemo, useState } from 'react';
import { KernelController, type ControlResult, type DriverLayout, type TurnIntent } from '../kernel';

type Props = { selectedPieces: readonly string[]; onClearSelection: () => void; onRemovePiece: (piece: string) => void; controller: KernelController; revision: string; layout?: DriverLayout;
  busy: boolean; onTurn: (intent: TurnIntent) => void };

export function FamilyControls(props: Props) {
  const { controller, revision, selectedPieces, onClearSelection, onRemovePiece } = props;
  const catalog = controller.controls;
  const [choice, setFamily] = useState(catalog.families[0].id);
  const selection = JSON.stringify(selectedPieces);
  const matching = useMemo(() => selectedPieces.length ? controller.familiesForPieces(selectedPieces) :
    { status: 'Matched', revision, families: catalog.families.map(f => f.id) }, [controller, revision, selection]);
  const matches = matching.status === 'Matched' && matching.revision === revision ? matching.families ?? [] : [];
  const familyId = matches.length === 1 ? matches[0] : matches.includes(choice) ? choice : '';
  useEffect(() => { if (matches.length === 1) setFamily(matches[0]); }, [JSON.stringify(matches)]);
  return <section className="family-controls" aria-label="Twist family controller" data-matching-families={matches.join(',')}>
    <div className="family-piece-selection">
      <p role="status" data-testid="family-piece-selection">{selectedPieces.length ?
        `${selectedPieces.length} selected ${selectedPieces.length === 1 ? 'piece' : 'pieces'} · ${matches.length} matching ${matches.length === 1 ? 'family' : 'families'}` :
        'Click pieces to choose a twist family. Click again to deselect.'}</p>
      {selectedPieces.length > 0 && <><div className="selected-piece-chips">{selectedPieces.map(id =>
        <button key={id} type="button" aria-label={`Remove ${id} from selection`} onClick={() => onRemovePiece(id)}>{id} ×</button>)}</div>
        <button type="button" onClick={onClearSelection}>Clear piece selection</button></>}
    </div>
    <label>Twist family <select aria-label="Twist family" value={familyId} onChange={e => setFamily(e.target.value)} disabled={!matches.length}>
      {!familyId && <option value="">{matches.length ? 'Choose a matching family' : 'No matching family'}</option>}
      {catalog.families.filter(f => matches.includes(f.id)).map(f => <option key={f.id} value={f.id}>{f.label ?? f.id}</option>)}
    </select></label>
    {selectedPieces.length > 0 && matches.length > 1 && <div className="family-options" aria-label="Matching twist families">
      {catalog.families.filter(f => matches.includes(f.id)).map(f =>
        <button key={f.id} type="button" aria-pressed={f.id === familyId} onClick={() => setFamily(f.id)}>Choose {f.label ?? f.id} family</button>)}
    </div>}
    {familyId ? <FamilyStops key={familyId} {...props} familyId={familyId} /> :
      <p className="subtle">{matching.status !== 'Matched' ? 'Piece-based family selection is unavailable in this runtime.' : matches.length ?
        'Choose one of the matching families, then pick an angular destination.' :
        'No twist family includes all selected pieces from this position. Remove a piece or clear the selection.'}</p>}
  </section>;
}

function FamilyStops({ controller, revision, layout, busy, onTurn, familyId }: Props & { familyId: string }) {
  const catalog = controller.controls;
  const [current, setCurrent] = useState<Record<string, string>>({});
  const [previews, setPreviews] = useState<Record<string, ControlResult>>({});
  const [draft, setDraft] = useState<Record<string, string>>({});
  const [hover, setHover] = useState<Record<string, string>>();
  const [unavailable, setUnavailable] = useState('');
  const family = catalog.families.find(f => f.id === familyId)!;
  const drivers = family.drivers.map(id => catalog.drivers.find(d => d.id === id)!);
  const positions = useMemo(() => Object.fromEntries(catalog.drivers.map(driver => {
    const nodes: { angle: number; radius: number; x: number; y: number }[] = [];
    driver.stops.forEach((stop, index) => {
      const angle = layout?.drivers[driver.id]?.angles[index] ?? index * 2 * Math.PI / driver.stops.length;
      const candidates = [104, 62, 126, 82].map(radius => ({ angle, radius, x: radius * Math.sin(angle), y: -radius * Math.cos(angle) }));
      const spacing = (p: typeof candidates[number]) => Math.min(...nodes.map(q => Math.hypot(p.x-q.x,p.y-q.y)), Infinity);
      nodes.push(candidates.find(p => spacing(p)>=42) ?? candidates.reduce((a,b) => spacing(a)>=spacing(b)?a:b));
    });
    return [driver.id, nodes];
  })), [catalog, layout]);
  const key = (targets: Record<string, string>) => family.drivers.map(id => targets[id]).join('/');

  useEffect(() => {
    let cancelled = false; let timer: ReturnType<typeof setTimeout> | undefined;
    const state = controller.familyState(familyId);
    setCurrent(state.current ?? {}); setDraft(state.current ?? {}); setPreviews({}); setHover(undefined);
    setUnavailable(state.status === 'Ready' ? '' : 'This family has no phase frame in the current state.');
    if (state.status !== 'Ready' || busy) return;
    // One destination per task keeps rendering responsive, even for large catalogs.
    const next = (offset: number) => {
      if (cancelled) return;
      const page = controller.destinations(familyId, offset, 1);
      if (cancelled || page.revision !== revision) return;
      setPreviews(previous => ({ ...previous, ...Object.fromEntries((page.destinations ?? [])
        .map(result => [key(result.targets!), result])) }));
      if (page.nextOffset! < page.total!) timer = setTimeout(() => next(page.nextOffset!), 0);
    };
    timer = setTimeout(() => next(0), 0);
    return () => { cancelled = true; if (timer !== undefined) clearTimeout(timer); };
  }, [controller, revision, familyId, busy]);

  const selected = previews[key(draft)];
  const inspected = previews[key(hover ?? draft)];
  const path = inspected?.pathId ? layout?.paths?.[inspected.pathId] : undefined;
  return <>
    {unavailable ? <p role="status">{unavailable}</p> : <>
      <div className="stop-rings">{drivers.map(driver => {
        const sourceAngle = layout?.drivers[driver.id]?.angles[driver.stops.indexOf(current[driver.id])];
        const delta = path?.[driver.id];
        const arc = sourceAngle !== undefined && delta !== undefined && Math.abs(delta)>1e-8 && Math.abs(delta)<2*Math.PI-1e-8 ?
          `M ${140+104*Math.sin(sourceAngle)} ${140-104*Math.cos(sourceAngle)} A 104 104 0 ${Math.abs(delta)>Math.PI?1:0} ${delta>0?1:0} ${140+104*Math.sin(sourceAngle+delta)} ${140-104*Math.cos(sourceAngle+delta)}` : undefined;
        return <div key={driver.id}>
        <div className="stop-ring" role="group" aria-label={`${driver.id} angular stops`}>
          <svg viewBox="0 0 280 280" aria-hidden="true"><defs><marker id={`direction-${driver.id}`} viewBox="0 0 10 10" refX="9" refY="5" markerWidth="5" markerHeight="5" orient="auto-start-reverse"><path d="M 0 0 L 10 5 L 0 10 z" fill="#7cdddf" /></marker></defs>
            <circle cx="140" cy="140" r="104" />
            {positions[driver.id].filter(p => p.radius!==104).map((p,i) => <line key={i}
              x1={140+104*Math.sin(p.angle)} y1={140-104*Math.cos(p.angle)} x2={140+p.x} y2={140+p.y} stroke="#82949e" />)}
            {arc && <path d={arc} fill="none" stroke="#7cdddf" strokeWidth="3" markerEnd={`url(#direction-${driver.id})`} />}</svg>
          <div className="ring-center"><strong>{driver.id}</strong><span aria-live="polite" data-testid={`current-stop-${driver.id}`}>Current: {current[driver.id] ?? '…'}</span></div>
          {driver.stops.map((stop, index) => {
            const position = positions[driver.id][index], angle=position.angle;
            const targets = { ...draft, [driver.id]: stop };
            const preview = previews[key(targets)];
            const isCurrent = current[driver.id] === stop;
            const blocked = preview?.status === 'Blocked' || preview?.status === 'NoDirectRoute';
            return <button key={stop} type="button" data-stop={stop} data-status={preview?.status ?? 'Pending'}
              disabled={!current[driver.id]}
              className={`angular-stop ${isCurrent ? 'current-stop' : ''} ${blocked ? 'blocked-stop' : ''} ${draft[driver.id] === stop ? 'draft-stop' : ''}`}
              style={{ left: `${140 + position.x}px`, top: `${140 + position.y}px` }}
              aria-label={`${driver.id} destination ${stop}${isCurrent ? ', current' : ''}`} aria-current={isCurrent ? 'step' : undefined}
              title={`${stop} · ${(angle * 180 / Math.PI).toFixed(2)}° · ${preview?.status ?? 'Checking…'}`}
              onMouseEnter={() => setHover(targets)} onMouseLeave={() => setHover(undefined)} onFocus={() => setHover(targets)} onBlur={() => setHover(undefined)}
              onKeyDown={event => {
                if (!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(event.key)) return;
                event.preventDefault(); event.stopPropagation();
                const nodes = event.currentTarget.parentElement!.querySelectorAll<HTMLButtonElement>('button[data-stop]');
                nodes[(index + (['ArrowLeft','ArrowUp'].includes(event.key) ? -1 : 1) + nodes.length) % nodes.length].focus();
              }}
              onClick={() => {
                setDraft(targets);
                if (drivers.length === 1 && preview?.status !== 'ChoiceRequired') onTurn({ family: familyId, targets });
              }}>{stop}</button>;
          })}
        </div>
      </div>})}</div>
      {drivers.length > 1 && <button className="primary" onClick={() => onTurn({ family: familyId, targets: draft })}>Apply paired turn</button>}
      {selected?.status === 'ChoiceRequired' && <div className="path-choices">{selected.paths?.map(path =>
        <button key={path.pathId} onClick={() => onTurn({ family: familyId, targets: draft, pathId: path.pathId })}>{path.pathId}</button>)}</div>}
      {path && <p role="status">{Object.entries(path).map(([id,angle]) => `${id}: ${angle>=0?'+':''}${(angle*180/Math.PI).toFixed(2)}°`).join(' · ')}</p>}
      <p className="subtle">{drivers.length>1 ? 'Choose both destinations, then Apply. Muted stops remain selectable while you build the pair.' : 'Choose a destination. The highlighted stop follows the puzzle state. Muted stops may be blocked.'}</p>
      <details><summary>Destination list</summary>{drivers.map(driver => <label key={driver.id}>{driver.id} destination <select
        aria-label={`${driver.id} destination list`} value={draft[driver.id] ?? ''} onChange={event => {
          const targets={...draft,[driver.id]:event.target.value}; setDraft(targets);
          if(drivers.length===1) onTurn({family:familyId,targets});
        }}>{driver.stops.map(stop => <option key={stop} value={stop}>{stop}{current[driver.id]===stop?' (current)':''}</option>)}</select></label>)}</details>
    </>}
  </>;
}
