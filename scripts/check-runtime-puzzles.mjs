import { checkRuntimeData } from '../tools/authoring/runtime-data.mjs';
try {
  const result=await checkRuntimeData();
  console.log(`Runtime data: ${result.puzzles} puzzles and ${result.realizations} realizations match their source inputs.`);
} catch(error) { console.error(error.message ?? error); process.exitCode=1; }
