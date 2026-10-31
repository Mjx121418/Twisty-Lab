// Optional compatibility/authoring entry; applications load it explicitly.
import { createKernel, type MainModule } from './index.js';
import type { RuntimeBundle } from './runtime.js';
export { Puzzle, createCubeSource, createGearSource, type PuzzleSource, type PuzzlePackage, type GearSource } from './index.js';
export { createKernel as createAuthoringKernel } from './index.js';
export type AuthoringOptions = { wasmURL?: string | URL; locateFile?: (filename: string, directory: string) => string };
export async function createAuthoring(options: AuthoringOptions = {}): Promise<MainModule> { return createKernel(options); }
export function compileRuntimeBundle(module: MainModule, source: string, realizations: readonly unknown[] = []): RuntimeBundle {
  const result = JSON.parse(module.runtimeArtifactsJSON(source, JSON.stringify(realizations))) as RuntimeBundle & { status?: string; diagnostics?: { message: string }[] };
  if (result.status === 'Invalid') throw new Error(result.diagnostics?.map(d => d.message).join('\n') ?? 'Cannot compile runtime package.');
  return result;
}
export function migrateSourceSession(module: MainModule, source: string, save: string): string {
  const result = JSON.parse(module.migrateSourceSessionJSON(source,save)) as { status: string; sessionJSON?: string; diagnostics?: { message: string }[] };
  if (result.status !== 'Converted' || typeof result.sessionJSON !== 'string') throw new Error(result.diagnostics?.map(d => d.message).join('\n') ?? 'Cannot migrate this source session.');
  return result.sessionJSON;
}
