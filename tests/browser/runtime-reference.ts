import {execFileSync} from 'node:child_process';
// Compile the independent source reference to the source-free session identity.
export function runtimeReference(args:string[]):any {
  return JSON.parse(execFileSync('build/native/twisty',[...args,'--artifact'],{encoding:'utf8'}));
}
