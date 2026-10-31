// Portable execution/controller/geometry SDK. Imports only compiler-free WASM.
import { createArtifactKernel } from './artifact.js';
import { GeometryInstance as SharedGeometryInstance } from './shared.js';
import type { MainModule, Session as NativeSession } from './generated/twisty-artifact.mjs';
import type { CompiledDefinition, Snapshot as LegacySnapshot, Transition as LegacyTransition,
  Result as LegacyResult, Request, StateSnapshot as LegacyStateSnapshot, ControlCatalog,
  ControlResult as LegacyControlResult, FamilySelection as LegacyFamilySelection, TurnIntent, Realization, SceneDescriptor as LegacySceneDescriptor } from './shared.js';
export * from './shared.js';
export type { MainModule };
export type SceneDescriptor = Omit<LegacySceneDescriptor, 'definitionDigest' | 'executionDigest' | 'interpretationDigest' | 'realizationDigest'> & {
  executionDigest: string; interpretationDigest: string; realizationDigest: string;
};
export class GeometryInstance extends SharedGeometryInstance<SceneDescriptor> {}
export type Snapshot = Omit<LegacySnapshot, 'definitionDigest'> & { executionDigest: string };
export type StateSnapshot = Omit<LegacyStateSnapshot, 'definitionDigest'> & { executionDigest: string };
export type Transition = Omit<LegacyTransition, 'definitionDigest' | 'witnessVersion'> & { executionDigest: string; witnessVersion: 2 };
export type Result = Omit<LegacyResult, 'snapshot' | 'transitions' | 'transition'> & {
  snapshot?: Snapshot | StateSnapshot; transitions?: Transition[]; transition?: Transition;
};
export type FamilySelection = Omit<LegacyFamilySelection, 'snapshot' | 'transitions' | 'transition'> & Result & { executionDigest?: string };
export type ControlResult = Omit<LegacyControlResult, 'snapshot' | 'transitions' | 'transition' | 'destinations'> & Result & {
  destinations?: ControlResult[]; executionDigest?: string;
};
export type RuntimeDefinition = Omit<CompiledDefinition, 'kind' | 'formatVersion' | 'definitionDigest' | 'ruleModule'> & {
  kind: 'twisty-presentation'; contractVersion: 1; executionDigest: string; legacyDefinitionDigest: string;
};
export type ControlDocument = {
  kind: 'twisty-controls'; contractVersion: 1; executionDigest: string; controlDigest: string;
  catalog: {
    version: 1;
    drivers: { id: string; piece: number; stops: string[]; reader: { placement: number; stop: number }[] }[];
    families: { id: string; label?: string; drivers: string[]; routes: {
      from: string[]; to: string[]; request: string; profile: string; pathId: string; preferred?: boolean;
    }[] }[];
  };
};
export type RuntimeBundle = {
  abstract: { executionDigest: string; [key: string]: unknown };
  controls: ControlDocument; presentation: RuntimeDefinition; interpretation: Record<string, unknown>;
  views: { realization: Record<string, unknown>; payload: string; presentation: Realization }[];
};
export type RuntimeDocuments = { abstract: string; controls: string; presentation: string };
export type RuntimeOptions = { wasmURL?: string | URL; locateFile?: (filename: string, directory: string) => string };
export async function createRuntime(options: RuntimeOptions = {}): Promise<MainModule> {
  const module = await createArtifactKernel(options);
  if (JSON.parse(module.artifactInfoJSON()).runtimeAPIVersion !== 1)
    throw new Error('Unsupported runtime API version.');
  return module;
}
export function runtimeDocuments(bundle: RuntimeBundle): RuntimeDocuments {
  return { abstract: JSON.stringify(bundle.abstract), controls: JSON.stringify(bundle.controls), presentation: JSON.stringify(bundle.presentation) };
}
export function runtimeViews(bundle: RuntimeBundle): Realization[] {
  const interpretation = JSON.stringify(bundle.interpretation);
  return bundle.views.map(view => ({ ...view.presentation, runtime: {
    interpretation, envelope: JSON.stringify(view.realization), payload: view.payload,
  } }));
}
export class RuntimeSession {
  readonly native: NativeSession;
  readonly definition: RuntimeDefinition;
  readonly definitionText: string;
  readonly documents: RuntimeDocuments;
  constructor(private readonly module: MainModule, source: RuntimeDocuments | RuntimeSession) {
    if (source instanceof RuntimeSession) {
      if (source.module !== module) throw new Error('A shared definition must belong to the same runtime.');
      const native = module.createSessionFromDefinition(source.native);
      if (!native) throw new Error('Cannot share runtime definition.');
      this.native = native; this.definition = source.definition; this.definitionText = source.definitionText; this.documents = source.documents;
    } else {
      const presentation = JSON.parse(source.presentation) as RuntimeDefinition;
      const artifact = JSON.parse(source.abstract) as { executionDigest: string };
      if (presentation.kind !== 'twisty-presentation' || presentation.contractVersion !== 1 || presentation.executionDigest !== artifact.executionDigest)
        throw new Error('Presentation pins another runtime artifact.');
      let native: NativeSession | null;
      try { native = module.createRuntimeSession(source.abstract, source.controls); }
      catch(error) {
        const diagnostics = JSON.parse(module.validateRuntimeJSON(source.abstract,source.controls));
        throw new Error(diagnostics.diagnostics?.map((d:{message:string})=>d.message).join('\n') ?? String(error));
      }
      if (!native) throw new Error('Cannot create runtime session.');
      this.native = native; this.definition = presentation; this.definitionText = source.abstract; this.documents = source;
    }
  }
  snapshot(): Snapshot { return JSON.parse(this.native.snapshotJSON()) as Snapshot; }
  stateSnapshot(): StateSnapshot { return JSON.parse(this.native.stateSnapshotJSON()) as StateSnapshot; }
  lightweightResults(enabled = true): void { this.native.lightweightResults(enabled); }
  stateText(): string { return this.native.stateJSON(); }
  plan(request: Request, stateText = this.stateText()): Result { return JSON.parse(this.native.planJSON(JSON.stringify(request), stateText)) as Result; }
  validateState(text: string): Result { return JSON.parse(this.native.validateStateJSON(text)) as Result; }
  execute(request: Request, revision = this.native.revision()): Result { return JSON.parse(this.native.executeJSON(JSON.stringify(request), revision)) as Result; }
  move(operation: string, revision = this.native.revision()): Result { return this.execute({ operation, parameters: {} }, revision); }
  run(notation: string, policy: string, revision = this.native.revision()): Result { return JSON.parse(this.native.runJSON(notation, policy, revision)) as Result; }
  undo(revision = this.native.revision()): Result { return JSON.parse(this.native.undoJSON(revision)) as Result; }
  redo(revision = this.native.revision()): Result { return JSON.parse(this.native.redoJSON(revision)) as Result; }
  scramble(seed: number, length: number, revision = this.native.revision()): Result { return JSON.parse(this.native.scrambleJSON(seed, length, revision)) as Result; }
  load(text: string): Result { return JSON.parse(this.native.loadJSON(text)) as Result; }
  save(presentation: unknown = {}): string { return this.native.saveWithPresentationJSON(JSON.stringify(presentation)); }
  dispose(): void { this.native.delete(); }
}
export class RuntimeController {
  readonly native: NonNullable<ReturnType<MainModule['createController']>>;
  readonly controls: ControlCatalog;
  constructor(module: MainModule, private readonly session: RuntimeSession) {
    const native = module.createController(session.native);
    if (!native) throw new Error('Cannot create runtime controller.');
    this.native = native;
    this.controls = JSON.parse(native.catalogJSON()) as ControlCatalog;
  }
  catalog(): ControlCatalog { return this.controls; }
  familyState(family: string): ControlResult { return JSON.parse(this.native.familyStateJSON(this.session.native, family)) as ControlResult; }
  familiesForPieces(pieces: readonly string[]): FamilySelection { return JSON.parse(this.native.familiesForPiecesJSON(this.session.native, JSON.stringify(pieces))) as FamilySelection; }
  destinations(family: string, offset = 0, limit = 16): ControlResult { return JSON.parse(this.native.destinationsJSON(this.session.native, family, offset, limit)) as ControlResult; }
  planTo(intent: TurnIntent, revision = this.session.native.revision()): ControlResult { return JSON.parse(this.native.planToJSON(this.session.native, JSON.stringify(intent), revision)) as ControlResult; }
  turnTo(intent: TurnIntent, revision = this.session.native.revision()): ControlResult { return JSON.parse(this.native.turnToJSON(this.session.native, JSON.stringify(intent), revision)) as ControlResult; }
  dispose(): void { this.native.delete(); }
}
