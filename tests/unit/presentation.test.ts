import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { resolveMovementAlias, validateMovementPresentation, type MovementPresentation, type RuntimeDefinition } from '../../kernel/runtime';

// Opaque IDs deliberately share no spelling with labels, families or aliases.
const vectors=JSON.parse(readFileSync('tests/contracts/presentation.json','utf8'));
const fixture = () => structuredClone(vectors.base) as Pick<RuntimeDefinition,'operations'> & { display: MovementPresentation };

describe('movement presentation contract', () => {
  it('resolves authored aliases independently of request and family spelling', () => {
    const definition=fixture();
    expect(resolveMovementAlias(definition,'right')).toBe('request-17');
    definition.display.operations['request-17'].label='A different name';
    expect(resolveMovementAlias(definition,'right')).toBe('request-17');
    expect(resolveMovementAlias(definition,'request-29')).toBe('request-29');
    expect(()=>resolveMovementAlias(definition,'missing')).toThrow('Unknown movement alias');
  });
  it('accepts removal of every display name', () => {
    const {operations}=fixture();
    expect(()=>validateMovementPresentation({operations})).not.toThrow();
    expect(resolveMovementAlias({operations},'request-17')).toBe('request-17');
  });
  it.each(vectors.cases)('shares Python conformance: $name', vector => {
    const definition=structuredClone(vectors.base);
    for(const patch of vector.patch) {
      const parent=patch.path.slice(0,-1).reduce((value: any,key: string | number)=>value[key],definition);
      const key=patch.path.at(-1);
      if(patch.remove)delete parent[key];else parent[key]=patch.value;
    }
    if(vector.valid)expect(()=>validateMovementPresentation(definition)).not.toThrow();
    else expect(()=>validateMovementPresentation(definition)).toThrow();
  });
});
