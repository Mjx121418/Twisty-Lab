import { useEffect, useMemo, useRef, useState } from 'react';
import { KernelController, KernelSession, runtimeDocuments, type MainModule, type RuntimeDocuments, type RuntimeBundle,
  type Realization, type Result, type Snapshot, type Transition, type TurnIntent } from './kernel';
import { loadRuntime } from './runtimeLoader';
import { presets, loadPreset, installBundle, gearEdges, type Preset } from './runtimePresets';
import { CellControls } from './controls/CellControls';
import type { S3FadeSettings } from './s3Fade';
import { FamilyControls } from './controls/FamilyControls';
import { RelativeControls, relativeTurnAngle } from './controls/RelativeControls';
import { RenderView } from './renderer';
import { SurfaceView } from './surfaceRenderer';
import { pieceDestinations, type PieceDestination } from './destinations';
import { defaultViews, moveView, restoreViews, viewLabels, type ViewId, type ViewSelection } from './viewSelection';
type PuzzleView = RenderView | SurfaceView;
type ViewDefinition = { id: ViewId; realization: Realization; surfaceMode?: 'square' | 'tiles' };
type MountedView = { view: PuzzleView; realizationId: string; generation: number; cameras: Map<ViewId, unknown> };
type Inspector = 'pieces' | 'history' | 'definition';

function availableViews(preset: Preset): ViewDefinition[] {
  if (preset.stereographic) return [{ id: 'stereographic', realization: preset.realizations[0] }];
  if (preset.surface) return [
    { id: 'fundamental-domain', realization: preset.realizations[0], surfaceMode: 'square' },
    { id: 'tiled-plane', realization: preset.realizations[1], surfaceMode: 'tiles' },
  ];
  const [cube, diagram] = preset.realizations;
  return [{ id: 'cube', realization: cube }, ...(preset.spherical ? [{ id: 'sphere' as const, realization: preset.spherical }] : []),
    ...(diagram ? [{ id: 'diagram' as const, realization: diagram }] : [])];
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
  const [customGear, setCustomGear] = useState<Preset>();
  const [pendingGear, setPendingGear] = useState<readonly string[]>();
  const [viewSelection, setViewSelection] = useState<ViewSelection>(() => defaultViews(['cube', 'sphere', 'diagram']));
  const [inspector, setInspector] = useState<Inspector>('pieces');
  const [algorithm, setAlgorithm] = useState("R U R' U'");
  const [policy, setPolicy] = useState('interactive');
  const [seed, setSeed] = useState('42');
  const [length, setLength] = useState('25');
  const [duration, setDuration] = useState(180);
  const [busy, setBusy] = useState(false);
  const [playing, setPlaying] = useState('');
  const [visualDigest, setVisualDigest] = useState('');
  const [selected, setSelected] = useState<string[]>([]);
  const [evidence, setEvidence] = useState<Result>();
  const [visualError, setVisualError] = useState('');
  const [fatalError, setFatalError] = useState('');
  const [lastScramble, setLastScramble] = useState('');
  const [s3Fade, setS3Fade] = useState<S3FadeSettings>();
  const kernel = useRef<KernelSession | undefined>(undefined);
  const controller = useRef<KernelController | undefined>(undefined);
  const advanced = useRef(false);
  const views = useRef<PuzzleView[]>([]);
  const mountedViews = useRef(new Map<ViewId, MountedView>());
  const cameraCache = useRef(new Map<ViewId, unknown>());
  const pendingCameras = useRef<Map<ViewId, unknown> | undefined>(undefined);
  const viewHosts = useRef(new Map<ViewId, HTMLDivElement>());
  const viewPicker = useRef<HTMLDetailsElement>(null);
  const progressBar = useRef<HTMLDivElement>(null);
  const sourceInput = useRef<HTMLInputElement>(null);
  const sessionInput = useRef<HTMLInputElement>(null);
  const busyRef = useRef(false);
  const speedRef = useRef(duration);
  const commands = useRef<(() => Result)[]>([]);
  const drain = useRef<() => void>(() => {});
  const animationFrame = useRef(0);
  const epoch = useRef(0);
  speedRef.current = duration;
  const availablePresets = customGear ? [...presets, customGear] : presets;
  const activePreset = snapshot && availablePresets.find((item) => item.realizations[0]?.compatibleDefinitionDigest === snapshot.executionDigest);
  const labels = { ...viewLabels, cube: activePreset?.euclideanLabel ?? viewLabels.cube };
  const viewDefinitions = useMemo(() => activePreset ? availableViews(activePreset) : [], [activePreset]);
  const orderedViews = viewSelection.order.flatMap(id => viewDefinitions.filter(view => view.id === id));
  const displayedViews = orderedViews.filter(view => viewSelection.visible.includes(view.id));
  const rhombus = ['rhombus-60','orbifold-333-rhombus'].includes(activePreset?.realizations[0].flatSurface?.chart??'');

  function relativeSession(session: KernelSession): boolean { return ['icosahedron-152-v1', 'tesseract3-v1', 'cell24-v1', 'cell24-192-v1'].includes(session.definition.puzzleId); }
  function readSnapshot(session: KernelSession): Snapshot {
    return (controller.current || relativeSession(session)) && !advanced.current ? { ...session.stateSnapshot(), legalRequests: [], candidateRequests: [] } : session.snapshot();
  }
  function replaceController(runtime: MainModule, session: KernelSession): void {
    controller.current?.dispose(); controller.current = undefined;
    const next = new KernelController(runtime, session);
    if (next.controls.families.length) { controller.current = next; session.lightweightResults(); }
    else { next.dispose(); if (relativeSession(session)) session.lightweightResults(); }
    advanced.current = false;
  }

  function install(runtime: MainModule, source: RuntimeDocuments, id: string, authored?: Preset): void {
    // Shipped presets replace the current puzzle. Release every old owner
    // before loading, so large catalogs do not overlap in the bounded WASM
    // heap. Unknown imports retain the previous session until loading passes.
    if (presets.some(preset => preset.source && preset.source === source)) {
      for (const { view } of mountedViews.current.values()) view.dispose();
      mountedViews.current.clear(); views.current = [];
      controller.current?.dispose(); controller.current = undefined;
      kernel.current?.dispose(); kernel.current = undefined;
      setSnapshot(undefined);
    }
    const next = new KernelSession(runtime, source);
    epoch.current++; cancelAnimationFrame(animationFrame.current);
    busyRef.current = false; setBusy(false); setPlaying('');
    replaceController(runtime, next);
    const initial = readSnapshot(next);
    if (authored) setCustomGear(authored);
    const selectedPreset = authored ?? presets.find(preset => preset.source === source);
    kernel.current?.dispose(); kernel.current = next;
    commands.current = [];
    cameraCache.current = new Map();
    pendingCameras.current = undefined;
    const preset = presets.find(item => item.realizations[0]?.compatibleDefinitionDigest === initial.executionDigest);
    const available: ViewId[] = preset ? availableViews(preset).map(view => view.id) : ['cube', 'sphere', 'diagram'];
    setViewSelection(current => current.order.length === available.length && current.order.every(id => available.includes(id)) ? current : defaultViews(available));
    setSourceId(id); setSnapshot(initial); setVisualDigest(initial.stateDigest);
    setSelected([]); setEvidence(undefined); setLastScramble(''); setVisualError('');
    setAlgorithm(initial.puzzleId.startsWith('cell24') ? "Xp_fXYZ Yp_vX (Xp_fXYZ Yp_vX)'" : initial.puzzleId === 'tesseract3-v1' ? "Xp_fY Yp_fZ (Xp_fY Yp_fZ)'" : initial.puzzleId === 'flat-333-6-v1' ? "A D A' D'" : initial.puzzleId.startsWith('flat-') ? "A B A' B'" : initial.puzzleId === 'helicopter-experimental-v1' ? 'UF_ab UL_af' :
      initial.puzzleId === 'bagua-experimental-v1' ? "U+ R' L' D2 R L U-" : initial.puzzleId.startsWith('icosahedron-') ? "A_j O_j (A_j O_j)'" : initial.puzzleId.startsWith('melty-') ? "U U'" :
      selectedPreset?.gearedEdges ? (selectedPreset.gearedEdges.length === 0 ? "Rq Uq Rq' Uq'" :
        next.plan({operation:'R',parameters:{}}).status === 'Legal' ? "R R'" : "Rq Rq'") : "R U R' U'");
    setGeneration((value) => value + 1);
  }

  async function selectPreset(preset: Preset): Promise<void> {
    if (!module) return;
    busyRef.current = true; setBusy(true);
    try {
      await loadPreset(preset);
      install(module, preset.source!, preset.id);
    }
    catch (error: unknown) {
      setEvidence({ status: 'Invalid', diagnostics: [{ reasonCode: 'source.invalid', source: preset.id, message: String(error) }] });
    } finally { busyRef.current = false; setBusy(false); }
  }

  useEffect(() => {
    let alive = true;
    Promise.all([loadRuntime(), loadPreset(presets[0])]).then(([runtime]) => {
      if (alive) { setModule(runtime); install(runtime, presets[0].source!, 'cube3'); }
    }).catch((error: unknown) => { if (alive) setFatalError(String(error)); });
    return () => {
      alive = false; epoch.current++; cancelAnimationFrame(animationFrame.current);
      for (const { view } of mountedViews.current.values()) view.dispose();
      mountedViews.current.clear(); views.current = [];
      controller.current?.dispose(); controller.current = undefined;
      kernel.current?.dispose(); kernel.current = undefined;
    };
  }, []);

  useEffect(() => {
    if (!module || !kernel.current) return;
    const session = kernel.current;
    const cameras = cameraCache.current;
    const restoredCameras = pendingCameras.current;
    pendingCameras.current = undefined;
    // Reconcile by view identity: reordering moves the existing canvas, while
    // hiding a view releases its renderer and retains its camera for later.
    for (const [id, entry] of mountedViews.current) {
      if (entry.generation === generation && displayedViews.some(view => view.id === id && view.realization.id === entry.realizationId)) continue;
      entry.cameras.set(id, entry.view.cameraState()); entry.view.dispose(); mountedViews.current.delete(id);
    }
    if (!activePreset) {
      views.current = [];
      setVisualError('No compatible realization is installed for this definition. The headless engine and inspector remain available.'); return;
    }
    try {
      for (const { id, realization, surfaceMode } of displayedViews) {
        const existing = mountedViews.current.get(id);
        if (existing) {
          if (restoredCameras?.has(id)) existing.view.restoreCamera(restoredCameras.get(id));
          continue;
        }
        const host = viewHosts.current.get(id);
        if (!host) continue;
        const view = surfaceMode
          ? new SurfaceView(module, host, session.native, realization, choosePiece, () => !busyRef.current, chooseDestination, surfaceMode)
          : new RenderView(module, host, session.native, realization, choosePiece, () => !busyRef.current, chooseDestination,
            id === 'cube' ? activePreset.cameraScale : undefined);
        view.restoreCamera(restoredCameras?.get(id) ?? cameras.get(id));
        view.setState(session.stateText());
        mountedViews.current.set(id, { view, realizationId: realization.id, generation, cameras });
      }
      setVisualError('');
    } catch (error: unknown) {
      setVisualError(`Visual limitation: ${String(error)}`);
    }
    restoredCameras?.forEach((camera, id) => cameras.set(id, camera));
    views.current = displayedViews.flatMap(({ id }) => { const entry = mountedViews.current.get(id); return entry ? [entry.view] : []; });
    setS3Fade(views.current.find((view): view is RenderView => view instanceof RenderView && !!view.fadeSettings)?.fadeSettings);
  }, [module, generation, viewSelection, viewDefinitions]);

  useEffect(() => { for (const view of views.current) view.highlight(selected, evidence?.implicatedPieces ?? []); }, [selected, evidence, generation, viewSelection, viewDefinitions]);

  useEffect(() => {
    if (!kernel.current || !snapshot || !views.current.length) return;
    for (const view of views.current) view.setDestinations(undefined, []);
    if (busy || !selected.length || controller.current || relativeSession(kernel.current)) return;
    const session = kernel.current;
    let timer: ReturnType<typeof setTimeout> | undefined;
    // Paint selection feedback before preparing optional endpoint ghosts.
    // Requests remain tied to their captured revision; cleanup cancels stale
    // work when selection, state, views, or the installed puzzle changes.
    const frame = requestAnimationFrame(() => { timer = setTimeout(() => {
      if (kernel.current !== session || session.native.revision() !== snapshot.revision) return;
      try {
        const destinations = pieceDestinations(session, snapshot, selected[0]);
        for (const view of views.current) view.setDestinations(selected[0], destinations);
      } catch (error: unknown) {
        for (const view of views.current) view.setDestinations(undefined, []);
        setVisualError(`Destination preview unavailable: ${String(error)}`);
      }
    }, 0); });
    return () => { cancelAnimationFrame(frame); if (timer !== undefined) clearTimeout(timer); };
  }, [selected, snapshot, busy, generation, viewSelection, viewDefinitions]);

  function choosePiece(piece?: string): void {
    const familySelection = controller.current || (kernel.current && relativeSession(kernel.current));
    setSelected(previous => !piece ? [] : !familySelection ? [piece] :
      previous.includes(piece) ? previous.filter(id => id !== piece) : [...previous, piece]);
  }

  function chooseDestination(destination: PieceDestination): void {
    const session = kernel.current;
    if (!session || busyRef.current || session.definition.executionDigest !== destination.executionDigest) return;
    enqueue(() => session.execute(destination.request, destination.revision));
  }

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
              for (const view of views.current) view.sample(progress * progress * (3 - 2 * progress));
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
        setVisualDigest(kernel.current.stateSnapshot().stateDigest);
        busyRef.current = false; setBusy(false); setPlaying('');
        if (progressBar.current) progressBar.current.style.width = '0%';
        drain.current();
      }
    }
  }

  drain.current = (): void => {
    if (busyRef.current || !kernel.current) return;
    const command = commands.current.shift(); if (!command) return;
    const result = command();
    setEvidence(['Blocked', 'Invalid', 'NoDirectRoute'].includes(result.status) ? result : undefined);
    setSnapshot(readSnapshot(kernel.current));
    if (result.scramble) setLastScramble(result.scramble.notation);
    if (result.transitionRecords?.length) {
      busyRef.current = true; setBusy(true); void animate(result);
    } else {
      for (const view of views.current) view.setState(kernel.current.stateText());
      setVisualDigest(kernel.current.stateSnapshot().stateDigest);
      drain.current();
    }
  };
  function enqueue(command: () => Result): void {
    if (!kernel.current) return;
    if (commands.current.length >= 100) { setEvidence({ status: 'Invalid', diagnostics: [{ reasonCode: 'queue.limit', source: '/commands', message: 'The command queue is full.' }] }); return; }
    commands.current.push(command); drain.current();
  }
  function turnTo(intent: TurnIntent): void {
    const active = controller.current, session = kernel.current;
    if (!active || !session) return;
    enqueue(() => active === controller.current && session === kernel.current ? active.turnTo(intent) : { status: 'StaleRevision' });
  }
  function resetPuzzle(): void {
    const current = kernel.current;
    if (!module || !current || busyRef.current) return;
    // Share the compiled definition, including imported/custom puzzles, while
    // starting a fresh controller history. Recreated views retain their cameras.
    const next = new KernelSession(module, current);
    epoch.current++; cancelAnimationFrame(animationFrame.current);
    replaceController(module, next);
    kernel.current = next; current.dispose();
    commands.current = [];
    const initial = readSnapshot(next);
    setSnapshot(initial); setVisualDigest(initial.stateDigest);
    setSelected([]); setEvidence(undefined); setLastScramble(''); setVisualError('');
    setGeneration((value) => value + 1);
  }

  useEffect(() => {
    const keydown = (event: KeyboardEvent): void => {
      if (event.key === 'Escape' && viewPicker.current?.open) {
        viewPicker.current.open = false; viewPicker.current.querySelector('summary')?.focus(); return;
      }
      const element = event.target as HTMLElement;
      if (event.repeat || ['INPUT', 'TEXTAREA', 'SELECT'].includes(element.tagName)) return;
      if (event.key === 'Escape') {
        setSelected([]);
      } else if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'z') {
        event.preventDefault(); enqueue(() => event.shiftKey ? kernel.current!.redo() : kernel.current!.undo());
      } else if (!event.ctrlKey && !event.metaKey && !event.altKey && /^[urfdlabce]$/i.test(event.key)) {
        event.preventDefault();
        const face = event.key.toUpperCase(), inverse = event.shiftKey;
        if (kernel.current?.definition.operations.some((move) => move.id === face)) enqueue(() => {
          const session = kernel.current!;
          const candidates = session.definition.operations.filter((move) => move.id === face || move.id === `${face}q`);
          const base = candidates.find((move) => session.plan({ operation: inverse ? move.inverse : move.id, parameters: {} }).status === 'Legal') ?? candidates.find((move) => move.id === face)!;
          return session.move(inverse ? base.inverse : base.id);
        });
      }
    };
    const dismissViews = (event: PointerEvent): void => {
      if (viewPicker.current?.open && !viewPicker.current.contains(event.target as Node)) viewPicker.current.open = false;
    };
    window.addEventListener('keydown', keydown);
    document.addEventListener('pointerdown', dismissViews);
    return () => { window.removeEventListener('keydown', keydown); document.removeEventListener('pointerdown', dismissViews); };
  }, []);

  async function configureGear(edges: string[]): Promise<void> {
    if (!module || busyRef.current) return;
    busyRef.current = true; setBusy(true); setPendingGear(edges);
    try {
      const authoring = await import('./authoring');
      const { bundle, source } = await authoring.compileGear(edges);
      const preset: Preset = { id:'gear-custom', label:'Gear family · custom', source:undefined, realizations:[], gearedEdges:edges, legacySource:source };
      installBundle(preset,bundle); install(module,preset.source!,'gear-custom',preset);
    } catch(error) { setEvidence({status:'Invalid',diagnostics:[{reasonCode:'authoring.invalid',source:'gear-custom',message:String(error)}]}); }
    finally { busyRef.current=false;setBusy(false);setPendingGear(undefined); }
  }
  async function importSource(file?: File): Promise<void> {
    if (!file || !module) return;
    try {
      if (file.size > 16 * 1024 * 1024) throw new Error('Definition import is limited to 16 MiB.');
      const text=await file.text(), parsed=JSON.parse(text);
      if (parsed.abstract && parsed.controls && parsed.presentation) {
        const preset:Preset={id:'imported',label:'Imported definition',source:undefined,realizations:[]};
        installBundle(preset,parsed as RuntimeBundle);install(module,preset.source!,'imported',preset);return;
      }
      const authoring=await import('./authoring');
      if (parsed.kind==='relational-gear-prototypes') {
        const {bundle,source}=await authoring.compileGear(parsed.gearedEdges,parsed.puzzleId,'imported');
        const preset:Preset={id:'imported',label:'Imported Gear family',source:undefined,realizations:[],gearedEdges:parsed.gearedEdges,legacySource:source};
        installBundle(preset,bundle);install(module,preset.source!,'imported',preset);return;
      }
      const bundle=await authoring.compileSource(text);
      const puzzleId=bundle.presentation.puzzleId;
      const matching=presets.find(p=>[p.id,p.id+'-v1',p.id+'-experimental-v1'].includes(puzzleId));
      if(matching)await loadPreset(matching);
      if(matching?.source && JSON.parse(matching.source.presentation).legacyDefinitionDigest===bundle.presentation.legacyDefinitionDigest) {
        install(module,matching.source,'imported');return;
      }
      const preset:Preset={id:'imported',label:'Imported definition',source:runtimeDocuments(bundle),realizations:[],legacySource:text};
      install(module,preset.source!,'imported',preset);
    } catch (error: unknown) {
      setEvidence({ status: 'Invalid', diagnostics: [{ reasonCode: 'source.invalid', source: file.name, message: String(error) }] });
    }
  }
  async function importSession(file?: File): Promise<void> {
    if (!file || !kernel.current) return;
    if (file.size > 4 * 1024 * 1024) { setEvidence({ status: 'Invalid', diagnostics: [{ reasonCode: 'document.limit', source: file.name, message: 'Session import is limited to 4 MiB.' }] }); return; }
    let text = await file.text();
    try {
      if (JSON.parse(text).schemaVersion===2) {
        const authoring=await import('./authoring');
        const source=activePreset?.legacySource ?? await authoring.presetSource(activePreset?.dataId??activePreset?.id??'');
        text=await authoring.migrateSave(source,text);
      }
    } catch(error) { setEvidence({status:'Invalid',diagnostics:[{reasonCode:'session.migration',source:file.name,message:String(error)}]});return; }
    const result = kernel.current.load(text);
    if (result.status !== 'Loaded') { setEvidence(result); return; }
    setSnapshot(readSnapshot(kernel.current)); setVisualDigest(kernel.current.stateSnapshot().stateDigest); setEvidence(undefined);
    for (const view of views.current) view.setState(kernel.current.stateText());
    const presentation = JSON.parse(text).presentation as { cameras?: unknown[] } | undefined;
    if (!presentation) return;
    const restored = restoreViews(presentation, viewDefinitions.map(view => view.id));
    if (Array.isArray(presentation?.cameras)) {
      pendingCameras.current = new Map(restored.cameraOrder.flatMap((id, index) => {
        const camera = presentation.cameras![index];
        return id && camera != null ? [[id, camera] as const] : [];
      }));
    }
    setViewSelection(restored.selection);
  }

  function saveSession(): void {
    if (!snapshot || !kernel.current) return;
    const order = orderedViews.map(view => view.id);
    download(`${snapshot.puzzleId}-session.json`, kernel.current.save({
      viewOrder: order, visibleViews: order.filter(id => viewSelection.visible.includes(id)),
      cameras: order.map(id => mountedViews.current.get(id)?.view.cameraState() ?? cameraCache.current.get(id) ?? null),
    }));
  }

  const definition = kernel.current?.definition;
  const definitionText = useMemo(() => kernel.current?.definitionText ?? '', [generation]);
  const legal = new Set(snapshot?.legalRequests.map((request) => request.operation));
  // Choose requests for the displayed source phase; core legalRequests remains
  // the authority for legality, including blockers on these same requests.
  const candidates = snapshot?.candidateRequests && new Set(snapshot.candidateRequests.map((request) => request.operation));
  const melty = activePreset?.turnAnglesDegrees;
  const icosahedron = activePreset?.id.startsWith('icosahedron-');
  const cellLayout = activePreset?.realizations[0]?.controlLayout;
  const s3 = cellLayout?.version === 2;
  const relative = activePreset?.id === 'icosahedron-152' || s3;
  const phaseRequests = definition?.operations.filter((operation) => candidates?.has(operation.id));
  const families = Array.from(new Set(definition?.operations.map((operation) => operation.family) ?? []));
  const faceOrder = activePreset?.surface ? ['A', 'B', 'C', 'D'] : icosahedron ? Array.from({ length: 20 }, (_, i) => String.fromCharCode(65+i)) : ['U', 'R', 'F', 'D', 'L', 'B'];
  families.sort((a, b) => (faceOrder.includes(a) ? faceOrder.indexOf(a) : 6) - (faceOrder.includes(b) ? faceOrder.indexOf(b) : 6) || a.localeCompare(b));
  const selectedPiece = definition?.pieces.find((piece) => piece.id === selected[0]);
  const selectedDomain = definition?.pieceTypes.find((type) => type.id === selectedPiece?.type)?.placementDomainId;
  const selectedPlacement = selectedPiece && snapshot ? definition?.placementDomains.find((domain) => domain.id === selectedDomain)?.placements.find((q) => q.key === snapshot.state.placementOf[selectedPiece.id]) : undefined;

  return (
    <div className="app" data-testid="app" data-animating={busy}>
      <header className="topbar">
        <div className="brand"><span className="brand-mark">◇</span><div><h1>Twisty Lab</h1><p>One puzzle. Every perspective.</p></div></div>
        <div className="top-actions">
          <button onClick={() => sourceInput.current?.click()} disabled={!snapshot || busy}>Import definition</button>
          <button onClick={() => sessionInput.current?.click()} disabled={!snapshot || busy}>Open session</button>
          <button className="primary" disabled={!snapshot || busy} onClick={saveSession}>Save session ↗</button>
        </div>
        <input ref={sourceInput} aria-label="Import definition file" className="file-input" type="file" accept=".json,application/json" onChange={(event) => { void importSource(event.target.files?.[0]); event.target.value = ''; }} />
        <input ref={sessionInput} aria-label="Open session file" className="file-input" type="file" accept=".json,application/json" onChange={(event) => { void importSession(event.target.files?.[0]); event.target.value = ''; }} />
      </header>

      <main>
        <section className="workspace">
          <div className="workspace-toolbar">
            <div><span className="eyebrow">PUZZLE WORKSPACE</span><select aria-label="Puzzle" value={sourceId} disabled={!module || busy} onChange={(event) => { const preset = availablePresets.find((item) => item.id === event.target.value); if (preset) void selectPreset(preset); }}>
              {presets.map((preset) => <option key={preset.id} value={preset.id}>{preset.label}</option>)}
              {sourceId === 'gear-custom' && <option value="gear-custom">Gear family · custom</option>}
              {sourceId === 'imported' && <option value="imported">Imported definition</option>}
            </select></div>
            <div className="view-options">
              {activePreset && <details ref={viewPicker} className="view-picker">
                <summary>Views <span>{displayedViews.length} shown</span></summary>
                <fieldset disabled={busy}>
                  <legend>Available views</legend>
                  <p>Select views and arrange their display order.</p>
                  <ol>{orderedViews.map(({ id }, index) => <li key={id} data-view-option={id}>
                    <label><input type="checkbox" checked={viewSelection.visible.includes(id)}
                      disabled={viewSelection.visible.includes(id) && displayedViews.length === 1}
                      onChange={(event) => setViewSelection(current => ({ ...current, visible: event.target.checked ? [...current.visible, id] : current.visible.filter(value => value !== id) }))} />{labels[id]}</label>
                    <div className="view-order-controls">
                      <button aria-label={`Move ${labels[id]} earlier`} disabled={index === 0} onClick={() => setViewSelection(current => moveView(current, id, -1))}>↑</button>
                      <button aria-label={`Move ${labels[id]} later`} disabled={index === orderedViews.length - 1} onClick={() => setViewSelection(current => moveView(current, id, 1))}>↓</button>
                    </div>
                  </li>)}</ol>
                  <button className="restore-views" onClick={() => setViewSelection(defaultViews(viewDefinitions.map(view => view.id)))}>Restore default views</button>
                </fieldset>
              </details>}
            </div>
          </div>
          {activePreset?.gearedEdges && <fieldset className="gear-configuration" disabled={busy}>
            <legend>Geared edges · changing these starts a solved puzzle</legend>
            {gearEdges.map((edge) => {
              const checked = (pendingGear ?? activePreset.gearedEdges!).includes(edge);
              return <label key={edge}><input type="checkbox" aria-label={`Gear edge ${edge}`} checked={checked} onChange={() => {
                if (!module) return;
                const geared = new Set<string>(pendingGear ?? activePreset.gearedEdges);
                if (checked) geared.delete(edge); else geared.add(edge);
                void configureGear(gearEdges.filter(id=>geared.has(id)));
              }} />{edge}</label>;
            })}
            <small>Face keys choose coupled turns when any middle-layer edge is geared, otherwise ordinary quarter-turns. F = coupled turn; Fq = ordinary turn.</small>
          </fieldset>}

          <div className={`views view-count-${displayedViews.length}`}>
            {displayedViews.map(({ id }, index) => <article key={id} className={`view-panel ${id === 'diagram' ? 'diagram-panel' : ''}`} data-view={id}>
              <div className="view-title"><span>{String(index + 1).padStart(2, '0')} / {labels[id]}</span><span className="subtle">{
                id === 'fundamental-domain' ? 'Drag to pan' : id === 'tiled-plane' ? 'Drag to pan · scroll to zoom' :
                id === 'sphere' ? 'Drag to rotate freely · scroll to zoom' : id === 'cube' || id === 'stereographic' ? 'Drag to orbit · scroll to zoom' : (controller.current || relative) ? 'Click pieces to choose a twist family' : 'Click a piece, then a translucent destination'
              }</span></div>
              <div ref={host => { if (host) viewHosts.current.set(id, host); else viewHosts.current.delete(id); }} className="canvas-host"
                data-testid={index === 0 ? 'cube-view' : index === 1 ? 'diagram-view' : 'extra-view'} data-view-id={id} data-state-digest={visualDigest} />
            </article>)}
          </div>

          {(fatalError || visualError) && <div className="notice" role="alert">{fatalError || visualError}</div>}
          {!snapshot && !fatalError && <div className="loading">Loading the puzzle engine…</div>}

          <div className="state-strip">
            <span className={`status-dot ${snapshot?.solved ? 'solved' : ''}`} /><strong data-testid="goal-status">{snapshot ? snapshot.solved ? 'Solved' : 'Exploring' : 'Loading'}</strong>
            <span className="state-label">EXACT STATE</span><code data-testid="state-digest" title={snapshot?.stateDigest}>{snapshot?.stateDigest ?? '—'}</code>
            <span className="revision">rev. {snapshot?.revision ?? '0'}</span>
          </div>

          <section className="commands">
            <div className="section-heading"><h2>Move the puzzle</h2><span className="subtle">{s3 ? `${cellLayout.groups.length} ${cellLayout.groups.length === 24 ? 'octahedral' : 'cubic'} cells · face, edge and vertex axes` : activePreset?.surface ? `Face keys ${Object.keys(activePreset.realizations[0].flatSurface?.diskCenters??{}).join(' ')} · ${rhombus ? 60 : 90}° turns · Shift for inverse` : icosahedron ? '20 face axes · ordinary turns: 120° · select pieces to choose an axis' : snapshot?.puzzleId === 'helicopter-experimental-v1' ? 'Stops a–f · buttons follow each grip’s current phase' : snapshot?.puzzleId === 'bagua-experimental-v1' ? '+ / −: 45° · face keys: 90° · Shift for inverse' : melty ? 'Face keys U R F D L B · choose each cap independently · hover for angles' : 'Face keys U R F D L B · Shift for inverse'}</span></div>
            {controller.current && snapshot && <FamilyControls key={generation} controller={controller.current} revision={snapshot.revision}
              layout={cellLayout?.version === 1 ? cellLayout : undefined} busy={busy} onTurn={turnTo}
              selectedPieces={selected} onClearSelection={() => setSelected([])} onRemovePiece={choosePiece} />}
            {relative && !s3 && kernel.current && snapshot && <RelativeControls key={generation} session={kernel.current} revision={snapshot.revision}
              history={snapshot.history} cursor={snapshot.cursor}
              realization={activePreset?.realizations[0]} busy={busy} selectedPieces={selected} onClearSelection={() => setSelected([])}
              onMove={(operation, revision) => {
                const session = kernel.current!;
                enqueue(() => session === kernel.current ? session.move(operation, revision) : { status: 'StaleRevision' });
              }} />}
            {s3 && kernel.current && snapshot && <CellControls key={generation} session={kernel.current} revision={snapshot.revision}
              layout={cellLayout} busy={busy} selectedPieces={selected} onClearSelection={() => setSelected([])}
              onFocus={cell => views.current.forEach(view => { if (view instanceof RenderView) view.focusCell(cell); })}
              onGuide={(cell, axis) => views.current.forEach(view => { if (view instanceof RenderView) view.setCellGuide(cell, axis); })}
              onShell={() => views.current.forEach(view => { if (view instanceof RenderView) view.toggleOuterShell(); })}
              fade={s3Fade} onFadeRadius={(radius, boundary) => {
                views.current.forEach(view => { if (view instanceof RenderView && view.fadeSettings) {
                  if (boundary === 'end') view.setFadeEndRadius(radius); else view.setFadeStartRadius(radius);
                  setS3Fade(view.fadeSettings);
                } });
              }}
              onMove={(operation, revision) => { const session = kernel.current!;
                enqueue(() => session === kernel.current ? session.move(operation, revision) : { status: 'StaleRevision' }); }} />}
            <details key={`advanced-${generation}`} className="advanced-moves" open={(controller.current || relative) ? undefined : true} onToggle={event => {
              advanced.current = event.currentTarget.open;
              if (kernel.current) setSnapshot(readSnapshot(kernel.current));
            }}><summary>Advanced movement list</summary>
            <div className="moves">{families.map((family) => <div className={`move-pair ${(phaseRequests?.filter((op) => op.family === family).length ?? 0) > 2 ? 'many-moves' : ''}`} key={family}>{phaseRequests?.filter((operation) => operation.family === family).map((operation) => {
              const inverse = operation.id === `${family}'` || operation.id === `${family}_inv`;
              const half = operation.id === `${family}_half`;
              const angle = melty ? operation.id.split('+').map(id => `${melty[id].toFixed(3)}°`).join(' / ') : icosahedron ?
                `${(relativeTurnAngle(activePreset?.realizations[0],operation.id)*180/Math.PI).toFixed(3)}°` : undefined;
              return <button key={operation.id} title={angle} aria-label={`Move ${inverse ? `${family} inverse` : half ? `${family} half turn` : operation.id}`} className={!legal.has(operation.id) ? 'may-block' : ''} disabled={!snapshot} onClick={() => enqueue(() => kernel.current!.move(operation.id))}>{inverse ? '′' : half ? `${family}2` : operation.id}</button>;
            })}</div>)}
            </div></details>
              <div className="history-controls"><button disabled={!snapshot?.canUndo} onClick={() => enqueue(() => kernel.current!.undo())}>↶ Undo</button><button disabled={!snapshot?.canRedo} onClick={() => enqueue(() => kernel.current!.redo())}>↷ Redo</button><button disabled={!snapshot || busy} onClick={resetPuzzle}>Reset puzzle</button></div>
            <div className="algorithm-row"><textarea aria-label="Algorithm" value={algorithm} onChange={(event) => setAlgorithm(event.target.value)} spellCheck={false} rows={2} />
              <div className="algorithm-actions"><select aria-label="Execution policy" value={policy} onChange={(event) => setPolicy(event.target.value)}><option value="interactive">Keep legal prefix</option><option value="transactional">All or nothing</option></select><button className="primary" disabled={!snapshot} onClick={() => { const notation = algorithm, execution = policy; enqueue(() => kernel.current!.run(notation, execution)); }}>Play algorithm →</button></div>
            </div>
            <div className="playback-row"><label>Animation <select aria-label="Animation speed" value={duration} onChange={(event) => setDuration(Number(event.target.value))}><option value={0}>Off</option><option value={180}>Normal</option><option value={450}>Slow</option><option value={3000}>Inspect (3 s)</option></select></label><span aria-live="polite">{busy ? playing : 'Ready for the next move'}</span></div>
            <div className="progress-track"><div ref={progressBar} /></div>
          </section>

          <section className="scramble-section"><div><h2>Reproducible scramble</h2><p>A legal walk from the current state.</p></div><label>Seed<input aria-label="Scramble seed" type="number" min={0} max={4294967295} value={seed} onChange={(event) => setSeed(event.target.value)} /></label><label>Moves<input aria-label="Scramble length" type="number" min={0} max={1000} value={length} onChange={(event) => setLength(event.target.value)} /></label><button disabled={!snapshot} onClick={() => { const s = Number(seed), n = Number(length); enqueue(() => Number.isInteger(s) && s >= 0 && s <= 4294967295 && Number.isInteger(n) && n >= 0 && n <= 1000 ? kernel.current!.scramble(s, n) : { status: 'Invalid', diagnostics: [{ reasonCode: 'scramble.parameters', source: '/scramble', message: 'Use an integer seed and 0–1000 moves.' }] }); }}>Scramble ↗</button></section>
          {lastScramble && <code className="scramble-output" data-testid="scramble-output">{lastScramble}</code>}
        </section>

        <aside className="inspector">
          <div className="inspector-header"><span className="eyebrow">LOOK UNDER THE SURFACE</span><h2>Definition inspector</h2><p>Follow a piece from abstract placement to visual part.</p></div>
          <div className="metrics"><div><strong>{definition?.pieces.length ?? '—'}</strong><span>pieces</span></div><div><strong>{definition?.operations.length ?? '—'}</strong><span>primitives</span></div><div><strong>{definition?.symmetry?.members?.length ?? '—'}</strong><span>symmetries</span></div></div>
          {evidence && <section className="evidence" role="alert" data-testid="blocking-evidence"><span className="eyebrow">{evidence.status === 'Blocked' ? 'MOVE BLOCKED' : evidence.status === 'NoDirectRoute' ? 'TURN UNAVAILABLE' : 'INVALID INPUT'}</span><h3>{evidence.reasonCode === 'relation.rejected' ? 'The puzzle interaction rules block this move.' : evidence.reasonCode === 'controls.no_route' ? 'No direct turn is defined between these stops. Choose another target or use the single-cap family.' : evidence.diagnostics?.[0]?.message ?? evidence.reasonCode}</h3><code>{evidence.reasonCode ?? evidence.diagnostics?.[0]?.reasonCode}</code>{evidence.implicatedPieces?.map((piece) => <button key={piece} onClick={() => choosePiece(piece)}>{piece}</button>)}{evidence.evidence !== undefined && <details><summary>Abstract evidence</summary><pre>{JSON.stringify(evidence.evidence, null, 2)}</pre></details>}</section>}
          <div className="inspector-tabs">{(['pieces', 'history', 'definition'] as Inspector[]).map((value) => <button key={value} aria-pressed={inspector === value} onClick={() => setInspector(value)}>{value === 'definition' ? 'Source & rules' : value[0].toUpperCase() + value.slice(1)}</button>)}</div>
          {inspector === 'pieces' && <>
            <div className="piece-list">{definition?.pieces.map((piece) => <button key={piece.id} aria-label={`Select ${piece.id}`} aria-pressed={selected.includes(piece.id)} disabled={busy} onClick={() => choosePiece(piece.id)}><span>{piece.id}</span><small>{piece.type}</small></button>)}</div>
            <section className="piece-detail"><span className="eyebrow">SELECTED {selected.length > 1 ? 'PIECES' : 'PIECE'}</span><h3>{selected.length > 1 ? `${selected.length} pieces selected` : selected[0] ?? 'Choose a piece or port'}</h3>{selected.length > 1 ? <ul>{selected.map(id => <li key={id}><strong>{id}</strong><code>{snapshot?.state.placementOf[id]}</code></li>)}</ul> : selectedPiece && <><dl><dt>Placement</dt><dd><code>{snapshot?.state.placementOf[selectedPiece.id]}</code></dd><dt>Footprint</dt><dd>{selectedPlacement?.footprint.join(', ')}</dd><dt>Ports</dt><dd>{Object.entries(selectedPiece.portLabels).map(([port, label]) => `${port}: ${label}`).join(' · ')}</dd></dl><details><summary>Port attachments</summary><pre>{JSON.stringify(selectedPlacement?.portAttachment, null, 2)}</pre></details></>}</section>
          </>}
          {inspector === 'history' && <section className="history-list"><p>{snapshot?.cursor ?? 0} committed primitives at the cursor.</p>{snapshot?.history.length ? snapshot.history.map((move, index) => <div key={index} className={index >= snapshot.cursor ? 'future' : ''}><span>{String(index + 1).padStart(3, '0')}</span><strong>{move}</strong><small>{index >= snapshot.cursor ? 'redo' : 'committed'}</small></div>) : <p className="empty-state">Your first move starts a history.</p>}</section>}
          {inspector === 'definition' && <section className="definition-detail"><dl>{definition?.placementDomains.map((domain) => <div key={domain.id}><dt>{domain.id} domain</dt><dd>{domain.placements.length} exact placements</dd></div>)}</dl><h3>Position orbits & stabilizers</h3><dl>{Object.entries(definition?.symmetry?.positionOrbits ?? {}).map(([type, orbit]) => <div key={type}><dt>{type}</dt><dd>{orbit.positions.length} positions · |H| = {orbit.stabilizer.length}</dd></div>)}</dl><h3>Directed operations</h3><div className="operation-list">{definition?.operations.map((operation) => <details key={operation.id}><summary>{operation.id} <span>inverse {operation.inverse}</span></summary><pre>{JSON.stringify(operation, null, 2)}</pre></details>)}</div><details><summary>Compiled definition</summary><pre>{definitionText}</pre></details></section>}
          <div className="definition-footer"><span className="eyebrow">PINNED SEMANTICS</span><code title={snapshot?.executionDigest}>{snapshot?.executionDigest ?? '—'}</code></div>
        </aside>
      </main>
      <footer><span>Geometry interprets. The abstract core decides.</span><span>{busy ? 'Views follow the recorded transition.' : 'One authoritative puzzle state'}</span></footer>
    </div>
  );
}
