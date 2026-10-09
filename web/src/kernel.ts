import { createKernel, type MainModule } from '../../kernel/index';
import wasmURL from '../../kernel/generated/twisty.wasm?url';
export * from '../../kernel/index';

// Asset resolution belongs to the bundled app. The kernel has no Vite dependency.
let runtime: Promise<MainModule> | undefined;
export function loadRuntime(): Promise<MainModule> {
  runtime ??= createKernel({ wasmURL });
  return runtime;
}
