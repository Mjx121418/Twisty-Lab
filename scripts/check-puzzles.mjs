import { checkFreshness, readInventory, root, validatePuzzleData, wasmPuzzleLoader } from './puzzle-data.mjs';

try {
  const args = process.argv.slice(2);
  if (args.some((arg) => arg !== '--files-only')) throw new Error('Usage: npm run check:puzzles [-- --files-only]');
  const inventory = await readInventory();
  const manifest = await checkFreshness(root, inventory);
  console.log(`Prebuilt puzzles: ${Object.keys(manifest.artifacts).length} artifacts match their authoring inputs.`);
  if (!args.includes('--files-only')) {
    const { default: createModule } = await import('../kernel/generated/twisty.mjs');
    const module = await createModule();
    const result = await validatePuzzleData(root, inventory, wasmPuzzleLoader(module));
    console.log(`Current WASM kernel accepts ${result.puzzles} prebuilt puzzles and ${result.realizations} realizations; schemas and semantic pins agree.`);
  }
} catch (error) {
  console.error(error.message ?? error);
  process.exitCode = 1;
}
