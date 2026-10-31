// This entry point loads only the compiler-free WASM module.
import createModule, { type MainModule } from './generated/twisty-artifact.mjs';
export type { MainModule as ArtifactModule, Session as ArtifactSession,
  GeometryDefinition as ArtifactGeometryDefinition } from './generated/twisty-artifact.mjs';

export type ArtifactInfo = {
  artifactAPIVersion: 1; artifactVersions: number[]; interpretationVersions: number[];
  sessionSchemaVersions: number[]; witnessVersions: number[]; frameBufferVersion: number;
  realizationKinds: string[]; geometryCapabilities: string[];
};
export type ArtifactOptions = {
  wasmURL?: string | URL; locateFile?: (filename: string, directory: string) => string;
};
export async function createArtifactKernel(options: ArtifactOptions = {}): Promise<MainModule> {
  const locateFile = options.locateFile ?? ((filename: string, directory: string) =>
    filename.endsWith('.wasm') && options.wasmURL ? String(options.wasmURL) : directory + filename);
  const module = await createModule({ locateFile });
  const info = JSON.parse(module.artifactInfoJSON()) as ArtifactInfo;
  if (info.artifactAPIVersion !== 1 || info.frameBufferVersion !== 1)
    throw new Error('Unsupported artifact API or frame-buffer version.');
  return module;
}
export function loadArtifactSession(module: MainModule, artifactText: string) {
  const session = module.createArtifactSession(artifactText);
  if (!session) throw new Error('Artifact session factory returned no handle.');
  return session;
}
export function createArtifactGeometry(module: MainModule, session: Parameters<MainModule['createArtifactGeometryDefinition']>[0],
                                       interpretationText: string, realizationText: string) {
  const geometry = module.createArtifactGeometryDefinition(session, interpretationText, realizationText);
  if (!geometry) throw new Error('Artifact geometry factory returned no handle.');
  return geometry;
}
