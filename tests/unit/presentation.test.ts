import { describe, expect, it } from 'vitest';
import { resolveMovementAlias, validateMovementPresentation, type MovementPresentation } from '../../kernel/runtime';

// Opaque IDs deliberately share no spelling with labels, families or aliases.
const fixture = () => ({ operations: [
  { id:'request-17', inverse:'request-29', family:'group-8', transport:'map-3' },
  { id:'request-29', inverse:'request-17', family:'group-8', transport:'map-4' },
], display: {
  version:1, operations:{
    'request-17':{label:'Right cap',accessibleLabel:'Turn right cap',aliases:['right']},
    'request-29':{label:'Reverse',accessibleLabel:'Reverse right cap',aliases:['reverse']},
  }, familyOrder:['group-8'],familyProbes:{'group-8':'request-17'},keyboard:{R:['request-17']},
  interaction:'relative',instructions:'Choose an axis.',defaultAlgorithm:'request-17 request-29',
} satisfies MovementPresentation });

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
  it('counts Unicode labels in characters, matching authoring and JSON Schema', () => {
    const definition=fixture();
    definition.display.operations['request-17'].label='◇'.repeat(256);
    expect(()=>validateMovementPresentation(definition)).not.toThrow();
    definition.display.operations['request-17'].label='🧩'.repeat(256);
    expect(()=>validateMovementPresentation(definition)).not.toThrow();
    definition.display.operations['request-17'].label+='🧩';
    expect(()=>validateMovementPresentation(definition)).toThrow();
  });
  it('rejects aliases that collide with requests or other aliases', () => {
    const definition=fixture();
    definition.display.operations['request-17'].aliases=['request-29'];
    expect(()=>validateMovementPresentation(definition)).toThrow();
    definition.display.operations['request-17'].aliases=['reverse'];
    expect(()=>validateMovementPresentation(definition)).toThrow();
  });
  it('rejects unknown probe and keyboard request references', () => {
    const definition=fixture();
    definition.display.familyProbes['group-8']='missing';
    expect(()=>validateMovementPresentation(definition)).toThrow();
    definition.display.familyProbes['group-8']='request-17';
    definition.display.keyboard.R=['missing'];
    expect(()=>validateMovementPresentation(definition)).toThrow();
  });
});
