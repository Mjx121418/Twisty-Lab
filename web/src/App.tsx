import { useEffect, useMemo, useRef, useState } from 'react';
import type { MainModule } from '../../kernel/index';
import cubeSource from '../../packages/cube3/source.json?raw';
import bandageSource from '../../packages/bandaged/source.json?raw';
import cubeRealization from '../../packages/cube3/cube-euclidean.json';
import cubeDiagram from '../../packages/cube3/cube-port-diagram.json';
import cubeSphere from '../../packages/cube3/cube-spherical.json';
import bandageRealization from '../../packages/bandaged/cube-euclidean.json';
import bandageDiagram from '../../packages/bandaged/cube-port-diagram.json';
import bandageSphere from '../../packages/bandaged/cube-spherical.json';
import helicopterSource from '../../packages/helicopter/definition.json?raw';
import helicopterRealization from '../../packages/helicopter/helicopter-euclidean.json';
import helicopterDiagram from '../../packages/helicopter/helicopter-port-diagram.json';
import helicopterSphere from '../../packages/helicopter/helicopter-spherical.json';
import { KernelSession, loadRuntime, type Realization, type Result, type Snapshot, type Transition } from './kernel';
import { RenderView } from './renderer';

const presets = [
  { id: 'cube3', label: '3 × 3 cube', source: cubeSource, realizations: [cubeRealization, cubeDiagram], spherical: cubeSphere },
  { id: 'bandaged-uf-ufr', label: 'Bandaged cube · UF + UFR', source: bandageSource, realizations: [bandageRealization, bandageDiagram], spherical: bandageSphere },
  { id: 'helicopter', label: 'Helicopter Cube · jumbling', source: helicopterSource, realizations: [helicopterRealization, helicopterDiagram], spherical: helicopterSphere },
];
type Layout = 'both' | 'cube' | 'diagram';
type ViewPair = 'cube-diagram' | 'sphere-diagram' | 'cube-sphere';
type Inspector = 'pieces' | 'history' | 'definition';

function realizationPair(preset: typeof presets[number], pair: ViewPair): Realization[] {
  const [cube, diagram] = preset.realizations;
  return (preset.spherical && pair === 'sphere-diagram' ? [preset.spherical, diagram] :
    preset.spherical && pair === 'cube-sphere' ? [cube, preset.spherical] : [cube, diagram]) as Realization[];
}

function download(filename: string, content: string): void {
  const url = URL.createObjectURL(new Blob([content], { type: 'application/json' }));
  const link = document.createElement('a'); link.href = url; link.download = filename; link.click();
  setTimeout(() => URL.revokeObjectURL(url), 0);
}

export function App() {
  const [module, setModule] = useState<MainModule>();
  const [snapshot, setSnapshot] = useState<Snapshot>();
  const [generation, setGeneration] = useState(0);
  const [sourceId, setSourceId] = useState('cube3');
  const [layout, setLayout] = useState<Layout>('both');
  const [viewPair, setViewPair] = useState<ViewPair>('cube-diagram');
  const [inspector, setInspector] = useState<Inspector>('pieces');
  const [algorithm, setAlgorithm] = useState("R U R' U'");
  const [policy, setPolicy] = useState('interactive');
  const [seed, setSeed] = useState('42');
  const [length, setLength] = useState('25');
  const [duration, setDuration] = useState(180);
  const [busy, setBusy] = useState(false);
  const [playing, setPlaying] = useState('');
  const [visualDigest, setVisualDigest] = useState('');
  const [selected, setSelected] = useState<string>();
  const [evidence, setEvidence] = useState<Result>();
  const [visualError, setVisualError] = useState('');
  const [fatalError, setFatalError] = useState('');
  const [lastScramble, setLastScramble] = useState('');
  const kernel = useRef<KernelSession | undefined>(undefined);
  const views = useRef<RenderView[]>([]);
  const cameraCache = useRef(new Map<string, unknown>());
  const pendingCameras = useRef<Map<string, unknown> | undefined>(undefined);
  const cubeHost = useRef<HTMLDivElement>(null);
  const diagramHost = useRef<HTMLDivElement>(null);
  const progressBar = useRef<HTMLDivElement>(null);
  const sourceInput = useRef<HTMLInputElement>(null);
  const sessionInput = useRef<HTMLInputElement>(null);
  const busyRef = useRef(false);
  const speedRef = useRef(duration);
  const deferredLayout = useRef<Layout | undefined>(undefined);
  const commands = useRef<(() => Result)[]>([]);
  const drain = useRef<() => void>(() => {});
  const animationFrame = useRef(0);
  const epoch = useRef(0);
  speedRef.current = duration;
  const activePreset = presets.find((item) => item.realizations[0].compatibleDefinitionDigest === snapshot?.definitionDigest);
  const effectivePair = activePreset?.spherical ? viewPair : 'cube-diagram';

  function install(runtime: MainModule, source: string, id: string): void {
    const next = new KernelSession(runtime, source);
    const initial = next.snapshot();
    kernel.current?.dispose(); kernel.current = next;
    commands.current = [];
    cameraCache.current = new Map();
    pendingCameras.current = undefined;
    setSourceId(id); setSnapshot(initial); setVisualDigest(initial.stateDigest);
    setSelected(undefined); setEvidence(undefined); setLastScramble(''); setVisualError('');
    setAlgorithm(initial.puzzleId === 'helicopter-experimental-v1' ? 'UF_ab UL_af' : "R U R' U'");
    setGeneration((value) => value + 1);
  }

  useEffect(() => {
    let alive = true;
    loadRuntime().then((runtime) => {
      if (alive) { setModule(runtime); install(runtime, cubeSource, 'cube3'); }
    }).catch((error: unknown) => { if (alive) setFatalError(String(error)); });
    return () => {
      alive = false; epoch.current++; cancelAnimationFrame(animationFrame.current);
      kernel.current?.dispose(); kernel.current = undefined;
    };
  }, []);

  useEffect(() => {
    if (!module || !kernel.current || !cubeHost.current || !diagramHost.current) return;
    const session = kernel.current;
    const preset = presets.find((item) => item.realizations[0].compatibleDefinitionDigest === session.definition.definitionDigest);
    if (!preset) { setVisualError('No compatible realization is installed for this definition. The headless engine and inspector remain available.'); return; }
    const created: RenderView[] = [];
    const realizations = realizationPair(preset, viewPair);
    const cameras = cameraCache.current;
    const restoredCameras = pendingCameras.current;
    pendingCameras.current = undefined;
    try {
      for (let i = 0; i < 2; i++) {
        const host = i === 0 ? cubeHost.current : diagramHost.current;
        const view = new RenderView(module, host, session.native, realizations[i], setSelected, () => !busyRef.current);
        view.restoreCamera(restoredCameras?.get(realizations[i].id) ?? cameras.get(realizations[i].id));
        view.setState(session.stateText()); created.push(view);
      }
      views.current = created;
      setVisualError('');
    } catch (error: unknown) {
      for (const view of created) view.dispose();
      created.length = 0;
      setVisualError(`Visual limitation: ${String(error)}`);
    }
    return () => {
      created.forEach((view, index) => { cameras.set(realizations[index].id, view.cameraState()); view.dispose(); });
      views.current = [];
    };
  }, [module, generation, viewPair]);

  useEffect(() => { for (const view of views.current) view.highlight(selected, evidence?.implicatedPieces ?? []); }, [selected, evidence, generation, viewPair]);

  async function animate(result: Result): Promise<void> {
    const token = epoch.current;
    try {
      const records = result.transitionRecords ?? [];
      for (let i = 0; i < records.length; i++) {
        if (epoch.current !== token) return;
        const transition = JSON.parse(records[i]) as Transition;
        setPlaying(`${transition.request.operation} · ${i + 1} / ${records.length}`);
        for (const view of views.current) view.prepare(records[i]);
        const milliseconds = views.current.length ? speedRef.current : 0;
        if (milliseconds > 0) {
          await new Promise<void>((resolve) => {
            const start = performance.now();
            const tick = (now: number): void => {
              if (epoch.current !== token) { resolve(); return; }
              const progress = Math.min(1, (now - start) / milliseconds);
              for (const view of views.current) view.sample(progress);
              if (progressBar.current) progressBar.current.style.width = `${progress * 100}%`;
              if (progress === 1) resolve(); else animationFrame.current = requestAnimationFrame(tick);
            };
            animationFrame.current = requestAnimationFrame(tick);
          });
        } else for (const view of views.current) view.sample(1);
        setVisualDigest(transition.afterStateDigest);
      }
    } catch (error: unknown) {
      setVisualError(`Unsupported visual transition: ${String(error)}`);
    } finally {
      if (epoch.current === token && kernel.current) {
        for (const view of views.current) view.setState(kernel.current.stateText());
        setVisualDigest(kernel.current.snapshot().stateDigest);
        busyRef.current = false; setBusy(false); setPlaying('');
        if (progressBar.current) progressBar.current.style.width = '0%';
        if (deferredLayout.current) { setLayout(deferredLayout.current); deferredLayout.current = undefined; }
        drain.current();
      }
    }
  }

  drain.current = (): void => {
    if (busyRef.current || !kernel.current) return;
    const command = commands.current.shift(); if (!command) return;
    const result = command();
    setEvidence(result.status === 'Blocked' || result.status === 'Invalid' ? result : undefined);
    setSnapshot(kernel.current.snapshot());
    if (result.scramble) setLastScramble(result.scramble.notation);
    if (result.transitionRecords?.length) {
      busyRef.current = true; setBusy(true); void animate(result);
    } else {
      for (const view of views.current) view.setState(kernel.current.stateText());
      setVisualDigest(kernel.current.snapshot().stateDigest);
      drain.current();
    }
  };
  function enqueue(command: () => Result): void {
    if (!kernel.current) return;
    if (commands.current.length >= 100) { setEvidence({ status: 'Invalid', diagnostics: [{ reasonCode: 'queue.limit', source: '/commands', message: 'The command queue is full.' }] }); return; }
    commands.current.push(command); drain.current();
  }
  function requestLayout(value: Layout): void {
    if (busyRef.current) deferredLayout.current = value; else setLayout(value);
  }

  useEffect(() => {
    const keydown = (event: KeyboardEvent): void => {
      const element = event.target as HTMLElement;
      if (event.repeat || ['INPUT', 'TEXTAREA', 'SELECT'].includes(element.tagName)) return;
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'z') {
        event.preventDefault(); enqueue(() => event.shiftKey ? kernel.current!.redo() : kernel.current!.undo());
      } else if (!event.ctrlKey && !event.metaKey && !event.altKey && /^[urfdlb]$/i.test(event.key)) {
        event.preventDefault(); const operation = event.key.toUpperCase() + (event.shiftKey ? "'" : '');
        if (kernel.current?.definition.operations.some((move) => move.id === operation)) enqueue(() => kernel.current!.move(operation));
      }
    };
    window.addEventListener('keydown', keydown);
    return () => window.removeEventListener('keydown', keydown);
  }, []);

  async function importSource(file?: File): Promise<void> {
    if (!file || !module) return;
    try {
      if (file.size > 4 * 1024 * 1024) throw new Error('Definition import is limited to 4 MiB.');
      install(module, await file.text(), 'imported');
    } catch (error: unknown) {
      setEvidence({ status: 'Invalid', diagnostics: [{ reasonCode: 'source.invalid', source: file.name, message: String(error) }] });
    }
  }
  async function importSession(file?: File): Promise<void> {
    if (!file || !kernel.current) return;
    if (file.size > 4 * 1024 * 1024) { setEvidence({ status: 'Invalid', diagnostics: [{ reasonCode: 'document.limit', source: file.name, message: 'Session import is limited to 4 MiB.' }] }); return; }
    const text = await file.text(); const result = kernel.current.load(text);
    if (result.status !== 'Loaded') { setEvidence(result); return; }
    setSnapshot(kernel.current.snapshot()); setVisualDigest(kernel.current.snapshot().stateDigest); setEvidence(undefined);
    for (const view of views.current) view.setState(kernel.current.stateText());
    const presentation = JSON.parse(text).presentation as { layout?: Layout; viewPair?: ViewPair; cameras?: unknown[] } | undefined;
    if (presentation?.layout && ['both', 'cube', 'diagram'].includes(presentation.layout)) setLayout(presentation.layout);
    const pair = presentation?.viewPair && ['cube-diagram', 'sphere-diagram', 'cube-sphere'].includes(presentation.viewPair) ? presentation.viewPair : viewPair;
    if (Array.isArray(presentation?.cameras)) {
      if (pair === viewPair) presentation.cameras.forEach((camera, index) => views.current[index]?.restoreCamera(camera));
      else if (activePreset) pendingCameras.current = new Map(realizationPair(activePreset, pair).map((realization, index) => [realization.id, presentation.cameras![index]]));
    }
    setViewPair(pair);
  }

  const definition = kernel.current?.definition;
  const definitionText = useMemo(() => kernel.current?.definitionText ?? '', [generation]);
  const legal = new Set(snapshot?.legalRequests.map((request) => request.operation));
  // Choose requests for the displayed source phase; core legalRequests remains
  // the authority for legality, including blockers on these same requests.
  const phaseRequests = definition?.operations.filter((operation) => Object.entries(operation.pieceGuards ?? {}).every(([piece, placement]) => snapshot?.state.placementOf[piece] === placement));
  const families = Array.from(new Set(definition?.operations.map((operation) => operation.family) ?? []));
  const faceOrder = ['U', 'R', 'F', 'D', 'L', 'B'];
  families.sort((a, b) => (faceOrder.includes(a) ? faceOrder.indexOf(a) : 6) - (faceOrder.includes(b) ? faceOrder.indexOf(b) : 6) || a.localeCompare(b));
  const selectedPiece = definition?.pieces.find((piece) => piece.id === selected);
  const selectedDomain = definition?.pieceTypes.find((type) => type.id === selectedPiece?.type)?.placementDomainId;
  const selectedPlacement = selectedPiece && snapshot ? definition?.placementDomains.find((domain) => domain.id === selectedDomain)?.placements.find((q) => q.key === snapshot.state.placementOf[selectedPiece.id]) : undefined;

  return (
    <div className="app" data-testid="app" data-animating={busy}>
      <header className="topbar">
        <div className="brand"><span className="brand-mark">◇</span><div><h1>Twisty Lab</h1><p>One puzzle. Every perspective.</p></div></div>
        <div className="top-actions">
          <button onClick={() => sourceInput.current?.click()} disabled={!snapshot || busy}>Import definition</button>
          <button onClick={() => sessionInput.current?.click()} disabled={!snapshot || busy}>Open session</button>
          <button className="primary" disabled={!snapshot || busy} onClick={() => download(`${snapshot!.puzzleId}-session.json`, kernel.current!.save({ layout, viewPair: effectivePair, cameras: views.current.map((view) => view.cameraState()) }))}>Save session ↗</button>
        </div>
        <input ref={sourceInput} aria-label="Import definition file" className="file-input" type="file" accept=".json,application/json" onChange={(event) => { void importSource(event.target.files?.[0]); event.target.value = ''; }} />
        <input ref={sessionInput} aria-label="Open session file" className="file-input" type="file" accept=".json,application/json" onChange={(event) => { void importSession(event.target.files?.[0]); event.target.value = ''; }} />
      </header>

      <main>
        <section className="workspace">
          <div className="workspace-toolbar">
            <div><span className="eyebrow">PUZZLE WORKSPACE</span><select aria-label="Puzzle" value={sourceId} disabled={!module || busy} onChange={(event) => { const preset = presets.find((item) => item.id === event.target.value); if (preset && module) install(module, preset.source, preset.id); }}>
              {presets.map((preset) => <option key={preset.id} value={preset.id}>{preset.label}</option>)}
              {sourceId === 'imported' && <option value="imported">Imported definition</option>}
            </select></div>
            <div className="view-options">
              {activePreset?.spherical && <label className="realization-choice"><span className="eyebrow">GEOMETRY VIEWS</span><select aria-label="Geometry views" value={effectivePair} disabled={busy} onChange={(event) => setViewPair(event.target.value as ViewPair)}><option value="cube-diagram">Cube + diagram</option><option value="sphere-diagram">Sphere + diagram</option><option value="cube-sphere">Cube + sphere</option></select></label>}
              <div className="segmented" aria-label="View layout">{(['both', 'cube', 'diagram'] as Layout[]).map((value) => <button key={value} aria-pressed={layout === value} onClick={() => requestLayout(value)}>{value === 'both' ? 'Both views' : value === 'cube' ? effectivePair === 'sphere-diagram' ? 'Sphere' : 'Cube' : effectivePair === 'cube-sphere' ? 'Sphere' : 'Diagram'}</button>)}</div>
            </div>
          </div>

          <div className={`views layout-${layout}`}>
            <article className={`view-panel cube-panel ${layout === 'diagram' ? 'hidden' : ''}`}>
              <div className="view-title"><span>01 / {effectivePair === 'sphere-diagram' ? 'Spherical' : 'Euclidean'}</span><span className="subtle">Drag to orbit · scroll to zoom</span></div>
              <div ref={cubeHost} className="canvas-host" data-testid="cube-view" data-state-digest={visualDigest} />
              <span className="view-note">{effectivePair === 'sphere-diagram' ? 'Spherical regions · one exact state' : 'Rigid bodies & bound ports'}</span>
            </article>
            <article className={`view-panel diagram-panel ${layout === 'cube' ? 'hidden' : ''}`}>
              <div className="view-title"><span>02 / {effectivePair === 'cube-sphere' ? 'Spherical' : 'Port diagram'}</span><span className="subtle">{effectivePair === 'cube-sphere' ? 'Drag to orbit · click a region' : 'Click a port to inspect its piece'}</span></div>
              <div ref={diagramHost} className="canvas-host" data-testid="diagram-view" data-state-digest={visualDigest} />
              <span className="view-note">Same identities. Same transition.</span>
            </article>
          </div>

          {(fatalError || visualError) && <div className="notice" role="alert">{fatalError || visualError}</div>}
          {!snapshot && !fatalError && <div className="loading">Loading the puzzle engine…</div>}

          <div className="state-strip">
            <span className={`status-dot ${snapshot?.solved ? 'solved' : ''}`} /><strong data-testid="goal-status">{snapshot ? snapshot.solved ? 'Solved' : 'Exploring' : 'Loading'}</strong>
            <span className="state-label">EXACT STATE</span><code data-testid="state-digest" title={snapshot?.stateDigest}>{snapshot?.stateDigest ?? '—'}</code>
            <span className="revision">rev. {snapshot?.revision ?? '0'}</span>
          </div>

          <section className="commands">
            <div className="section-heading"><h2>Move the puzzle</h2><span className="subtle">{snapshot?.puzzleId === 'helicopter-experimental-v1' ? 'Stops a–f · buttons follow each grip’s current phase' : 'Face keys U R F D L B · Shift for inverse'}</span></div>
            <div className="moves">{families.map((family) => <div className={`move-pair ${definition?.operations.some((op) => op.family === family && op.pieceGuards) ? 'many-moves' : ''}`} key={family}>{phaseRequests?.filter((operation) => operation.family === family).map((operation) => <button key={operation.id} aria-label={`Move ${operation.id === `${family}'` ? `${family} inverse` : operation.id}`} className={!legal.has(operation.id) ? 'may-block' : ''} disabled={!snapshot} onClick={() => enqueue(() => kernel.current!.move(operation.id))}>{operation.id === `${family}'` ? '′' : operation.id}</button>)}</div>)}
              <div className="history-controls"><button disabled={!snapshot?.canUndo} onClick={() => enqueue(() => kernel.current!.undo())}>↶ Undo</button><button disabled={!snapshot?.canRedo} onClick={() => enqueue(() => kernel.current!.redo())}>↷ Redo</button></div>
            </div>
            <div className="algorithm-row"><textarea aria-label="Algorithm" value={algorithm} onChange={(event) => setAlgorithm(event.target.value)} spellCheck={false} rows={2} />
              <div className="algorithm-actions"><select aria-label="Execution policy" value={policy} onChange={(event) => setPolicy(event.target.value)}><option value="interactive">Keep legal prefix</option><option value="transactional">All or nothing</option></select><button className="primary" disabled={!snapshot} onClick={() => { const notation = algorithm, execution = policy; enqueue(() => kernel.current!.run(notation, execution)); }}>Play algorithm →</button></div>
            </div>
            <div className="playback-row"><label>Animation <select aria-label="Animation speed" value={duration} onChange={(event) => setDuration(Number(event.target.value))}><option value={0}>Off</option><option value={180}>Normal</option><option value={450}>Slow</option></select></label><span aria-live="polite">{busy ? playing : 'Ready for the next move'}</span></div>
            <div className="progress-track"><div ref={progressBar} /></div>
          </section>

          <section className="scramble-section"><div><h2>Reproducible scramble</h2><p>A legal walk from the current state.</p></div><label>Seed<input aria-label="Scramble seed" type="number" min={0} max={4294967295} value={seed} onChange={(event) => setSeed(event.target.value)} /></label><label>Moves<input aria-label="Scramble length" type="number" min={0} max={1000} value={length} onChange={(event) => setLength(event.target.value)} /></label><button disabled={!snapshot} onClick={() => { const s = Number(seed), n = Number(length); enqueue(() => Number.isInteger(s) && s >= 0 && s <= 4294967295 && Number.isInteger(n) && n >= 0 && n <= 1000 ? kernel.current!.scramble(s, n) : { status: 'Invalid', diagnostics: [{ reasonCode: 'scramble.parameters', source: '/scramble', message: 'Use an integer seed and 0–1000 moves.' }] }); }}>Scramble ↗</button></section>
          {lastScramble && <code className="scramble-output" data-testid="scramble-output">{lastScramble}</code>}
        </section>

        <aside className="inspector">
          <div className="inspector-header"><span className="eyebrow">LOOK UNDER THE SURFACE</span><h2>Definition inspector</h2><p>Follow a piece from abstract placement to visual part.</p></div>
          <div className="metrics"><div><strong>{definition?.pieces.length ?? '—'}</strong><span>pieces</span></div><div><strong>{definition?.operations.length ?? '—'}</strong><span>primitives</span></div><div><strong>{definition?.symmetry?.members?.length ?? '—'}</strong><span>symmetries</span></div></div>
          {evidence && <section className="evidence" role="alert" data-testid="blocking-evidence"><span className="eyebrow">{evidence.status === 'Blocked' ? 'MOVE BLOCKED' : 'INVALID INPUT'}</span><h3>{['footprint.partial_overlap', 'placement.blocked'].includes(evidence.reasonCode ?? '') ? 'A rigid piece crosses the turn boundary.' : evidence.diagnostics?.[0]?.message ?? evidence.reasonCode}</h3><code>{evidence.reasonCode ?? evidence.diagnostics?.[0]?.reasonCode}</code>{evidence.implicatedPieces?.map((piece) => <button key={piece} onClick={() => setSelected(piece)}>{piece}</button>)}{evidence.evidence !== undefined && <details><summary>Abstract evidence</summary><pre>{JSON.stringify(evidence.evidence, null, 2)}</pre></details>}</section>}
          <div className="inspector-tabs">{(['pieces', 'history', 'definition'] as Inspector[]).map((value) => <button key={value} aria-pressed={inspector === value} onClick={() => setInspector(value)}>{value === 'definition' ? 'Source & rules' : value[0].toUpperCase() + value.slice(1)}</button>)}</div>
          {inspector === 'pieces' && <>
            <div className="piece-list">{definition?.pieces.map((piece) => <button key={piece.id} aria-label={`Select ${piece.id}`} aria-pressed={selected === piece.id} onClick={() => setSelected(piece.id)}><span>{piece.id}</span><small>{piece.type}</small></button>)}</div>
            <section className="piece-detail"><span className="eyebrow">SELECTED PIECE</span><h3>{selected ?? 'Choose a piece or port'}</h3>{selectedPiece && <><dl><dt>Placement</dt><dd><code>{snapshot?.state.placementOf[selectedPiece.id]}</code></dd><dt>Footprint</dt><dd>{selectedPlacement?.footprint.join(', ')}</dd><dt>Ports</dt><dd>{Object.entries(selectedPiece.portLabels).map(([port, label]) => `${port}: ${label}`).join(' · ')}</dd></dl><details><summary>Port attachments</summary><pre>{JSON.stringify(selectedPlacement?.portAttachment, null, 2)}</pre></details></>}</section>
          </>}
          {inspector === 'history' && <section className="history-list"><p>{snapshot?.cursor ?? 0} committed primitives at the cursor.</p>{snapshot?.history.length ? snapshot.history.map((move, index) => <div key={index} className={index >= snapshot.cursor ? 'future' : ''}><span>{String(index + 1).padStart(3, '0')}</span><strong>{move}</strong><small>{index >= snapshot.cursor ? 'redo' : 'committed'}</small></div>) : <p className="empty-state">Your first move starts a history.</p>}</section>}
          {inspector === 'definition' && <section className="definition-detail"><dl>{definition?.placementDomains.map((domain) => <div key={domain.id}><dt>{domain.id} domain</dt><dd>{domain.placements.length} exact placements</dd></div>)}</dl><h3>Position orbits & stabilizers</h3><dl>{Object.entries(definition?.symmetry?.positionOrbits ?? {}).map(([type, orbit]) => <div key={type}><dt>{type}</dt><dd>{orbit.positions.length} positions · |H| = {orbit.stabilizer.length}</dd></div>)}</dl><h3>Directed operations</h3><div className="operation-list">{definition?.operations.map((operation) => <details key={operation.id}><summary>{operation.id} <span>inverse {operation.inverse}</span></summary><pre>{JSON.stringify(operation, null, 2)}</pre></details>)}</div><details><summary>Compiled definition & provenance</summary><pre>{definitionText}</pre></details></section>}
          <div className="definition-footer"><span className="eyebrow">PINNED SEMANTICS</span><code title={snapshot?.definitionDigest}>{snapshot?.definitionDigest ?? '—'}</code></div>
        </aside>
      </main>
      <footer><span>Geometry interprets. The abstract core decides.</span><span>{busy ? 'Views follow the recorded transition.' : 'Two views · one authoritative state'}</span></footer>
    </div>
  );
}
