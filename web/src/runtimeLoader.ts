import { createRuntime, type MainModule } from '../../kernel/runtime';
import wasmURL from '../../kernel/generated/twisty-artifact.wasm?url';
let runtime: Promise<MainModule> | undefined;
export function loadRuntime(): Promise<MainModule> { return runtime ??= createRuntime({wasmURL}); }
