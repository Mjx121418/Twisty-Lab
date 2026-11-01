import { SessionAdapter, ControllerAdapter } from './adapters.js';
// Portable execution/controller/geometry SDK. Imports only compiler-free WASM.
import { createArtifactKernel } from './artifact.js';
import { GeometryInstance as SharedGeometryInstance } from './shared.js';
import type { MainModule, Session as NativeSession } from './generated/twisty-artifact.mjs';
import type { CompiledDefinition, Snapshot as LegacySnapshot, Transition as LegacyTransition,
  Result as LegacyResult, StateSnapshot as LegacyStateSnapshot,
  ControlResult as LegacyControlResult, FamilySelection as LegacyFamilySelection, Realization, SceneDescriptor as LegacySceneDescriptor } from './shared.js';
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
export type GeometryBackendInfo = { registryVersion: 1; capabilities: string[]; backends: {
  id: string; version: number; envelopeVersions: number[]; payloadVersions: number[];
  capabilities: string[];
  /** Intrinsic space ID. Scene/frame descriptors specify its metric and buffer semantics. */
  space: 'R3' | 'E2' | 'S2' | 'S3';
}[] };
export function geometryBackends(module: { geometryBackendInfoJSON(): string }): GeometryBackendInfo {
  const info = JSON.parse(module.geometryBackendInfoJSON()) as GeometryBackendInfo;
  if (info.registryVersion !== 1) throw new Error('Unsupported geometry backend registry.');
  return info;
}
export function validateMovementPresentation(definition: Pick<RuntimeDefinition, 'operations' | 'display'>): void {
  const display = definition.display;
  if (display === undefined) return;
  const invalid = (): never => { throw new Error('Invalid movement presentation contract.'); };
  const bounded = (value: unknown, min: number, max: number): value is string =>
    typeof value === 'string' && Array.from(value).length >= min && Array.from(value).length <= max;
  const record = (value: unknown): value is Record<string, unknown> => !!value && typeof value === 'object' && !Array.isArray(value);
  const fields = (value: unknown, required: string[], optional: string[] = []): boolean => record(value) &&
    required.every(key => Object.hasOwn(value,key)) && Object.keys(value).every(key => required.includes(key) || optional.includes(key));
  if (!fields(display,['version','operations','familyOrder','familyProbes','keyboard','interaction','instructions','defaultAlgorithm']) ||
      display.version !== 1 || !record(display.operations) || !['moves','destinations','relative','cell-axes'].includes(display.interaction)) invalid();
  if (!Array.isArray(definition.operations) || !definition.operations.every(op => record(op) && bounded(op.id,1,256) && bounded(op.family,1,256))) invalid();
  const requests = new Map(definition.operations.map(op => [op.id, op]));
  const families = new Set(definition.operations.map(op => op.family));
  if (requests.size !== definition.operations.length || Object.keys(display.operations).length !== requests.size ||
      !Array.isArray(display.familyOrder) || display.familyOrder.length !== families.size ||
      new Set(display.familyOrder).size !== families.size || display.familyOrder.some(id => !families.has(id))) invalid();
  const aliases = new Set(requests.keys());
  for (const [id, entry] of Object.entries(display.operations)) {
    if (!requests.has(id) || !fields(entry,['label','accessibleLabel','aliases'],['angleDegrees']) ||
        ![entry.label, entry.accessibleLabel].every(s => bounded(s,1,256)) ||
        !Array.isArray(entry.aliases) || entry.aliases.length > 32 ||
        (entry.angleDegrees !== undefined && (!Array.isArray(entry.angleDegrees) || !entry.angleDegrees.every(Number.isFinite)))) invalid();
    for (const alias of entry.aliases) {
      if (!bounded(alias,1,256) || aliases.has(alias)) invalid();
      aliases.add(alias);
    }
  }
  if (!record(display.familyProbes) || Object.keys(display.familyProbes).length !== families.size) invalid();
  for (const [family,id] of Object.entries(display.familyProbes)) if (!families.has(family) || requests.get(id)?.family !== family) invalid();
  if (!record(display.keyboard)) invalid();
  for (const candidates of Object.values(display.keyboard))
    if (!Array.isArray(candidates) || !candidates.length || candidates.some(id => !requests.has(id))) invalid();
  if (![display.instructions, display.defaultAlgorithm].every(s => bounded(s,0,4096))) invalid();
}
/** Resolves one explicit authoring alias. Algorithm syntax still uses request IDs. */
export function resolveMovementAlias(definition: Pick<RuntimeDefinition, 'operations' | 'display'>, name: string): string {
  validateMovementPresentation(definition);
  if (definition.operations.some(op => op.id === name)) return name;
  const match = Object.entries(definition.display?.operations ?? {}).find(([,entry]) => entry.aliases.includes(name));
  if (!match) throw new Error('Unknown movement alias: '+name);
  return match[0];
}
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
export class RuntimeSession extends SessionAdapter<NativeSession, Snapshot, StateSnapshot, Result> {
  readonly definition: RuntimeDefinition;
  readonly definitionText: string;
  readonly documents: RuntimeDocuments;
  private readonly module: MainModule;
  constructor(module: MainModule, source: RuntimeDocuments | RuntimeSession) {
    let native: NativeSession, definition: RuntimeDefinition, documents: RuntimeDocuments;
    if (source instanceof RuntimeSession) {
      if (source.module !== module) throw new Error('A shared definition must belong to the same runtime.');
      const shared = module.createSessionFromDefinition(source.native);
      if (!shared) throw new Error('Cannot share runtime definition.');
      native = shared; definition = source.definition; documents = source.documents;
    } else {
      definition = JSON.parse(source.presentation) as RuntimeDefinition;
      const artifact = JSON.parse(source.abstract) as { executionDigest: string };
      if (definition.kind !== 'twisty-presentation' || definition.contractVersion !== 1 || definition.executionDigest !== artifact.executionDigest)
        throw new Error('Presentation pins another runtime artifact.');
      validateMovementPresentation(definition);
      let created: NativeSession | null;
      try { created = module.createRuntimeSession(source.abstract, source.controls); }
      catch(error) {
        const diagnostics = JSON.parse(module.validateRuntimeJSON(source.abstract,source.controls));
        throw new Error(diagnostics.diagnostics?.map((d:{message:string})=>d.message).join('\n') ?? String(error));
      }
      if (!created) throw new Error('Cannot create runtime session.');
      native = created; documents = source;
    }
    super(native);
    this.module = module; this.definition = definition; this.documents = documents; this.definitionText = documents.abstract;
  }
}
export class RuntimeController extends ControllerAdapter<NativeSession,
  NonNullable<ReturnType<MainModule['createController']>>, ControlResult, FamilySelection> {
  constructor(module: MainModule, session: RuntimeSession) {
    const native = module.createController(session.native);
    if (!native) throw new Error('Cannot create runtime controller.');
    super(native, session);
  }
}
