"""Optional presentation overrides. These never enter semantic contracts."""
import copy
import math
from .project import fields, require


def validate_movement_presentation(presentation):
    def check(condition, message='Invalid movement presentation contract.'):
        require(condition,message,'authoring.presentation')
    def text(value, minimum=1, maximum=256):
        return isinstance(value,str) and minimum<=len(value)<=maximum
    if 'display' not in presentation: return
    display=presentation['display']
    check(isinstance(display,dict))
    fields(display,['version','operations','familyOrder','familyProbes','keyboard','interaction','instructions','defaultAlgorithm'])
    check(type(display['version']) is int and display['version']==1)
    check(display['interaction'] in ('moves','destinations','relative','cell-axes'))
    operations=presentation['operations']
    check(isinstance(operations,list) and all(isinstance(op,dict) and text(op.get('id')) and text(op.get('family')) for op in operations))
    requests={op['id']:op for op in operations}
    check(len(requests)==len(operations))
    families={op['family'] for op in operations}
    check(isinstance(display['operations'],dict) and set(display['operations'])==set(requests),'Presentation must cover every request.')
    order=display['familyOrder']
    check(isinstance(order,list) and all(isinstance(key,str) for key in order) and len(order)==len(set(order)) and set(order)==families,
          'Presentation family order must list each family exactly once.')
    aliases=set(requests)
    for value in display['operations'].values():
        fields(value,['label','accessibleLabel','aliases'],['angleDegrees'])
        check(text(value['label']) and text(value['accessibleLabel']),'Display labels must contain 1–256 characters.')
        check(isinstance(value['aliases'],list) and len(value['aliases'])<=32)
        for alias in value['aliases']:
            check(text(alias) and alias not in aliases,'Aliases must be unique and cannot shadow request IDs.')
            aliases.add(alias)
        angles=value.get('angleDegrees',[])
        check(isinstance(angles,list) and all(type(angle) in (int,float) and math.isfinite(angle) for angle in angles),'Presentation angles must be finite.')
    probes=display['familyProbes']
    check(isinstance(probes,dict) and set(probes)==families)
    for family,request in probes.items():
        check(isinstance(request,str) and request in requests and requests[request]['family']==family)
    keyboard=display['keyboard']
    check(isinstance(keyboard,dict))
    for candidates in keyboard.values():
        check(isinstance(candidates,list) and candidates and all(isinstance(key,str) and key in requests for key in candidates))
    check(text(display['instructions'],0,4096) and text(display['defaultAlgorithm'],0,4096))


def apply_presentation(bundle, override=None):
    presentation = bundle['presentation']
    if override is None:
        validate_movement_presentation(presentation)
        return
    require('display' in presentation,'Presentation overrides require display metadata.','authoring.presentation')
    display = copy.deepcopy(presentation['display'])
    fields(override, ['version'], ['operations','familyOrder','instructions','defaultAlgorithm'])
    require(type(override['version']) is int and override['version']==1,'Unsupported presentation override version.')
    for key in ('familyOrder','instructions','defaultAlgorithm'):
        if key in override: display[key]=copy.deepcopy(override[key])
    require(isinstance(override.get('operations',{}),dict),'Expected operation presentation overrides.')
    for key,value in override.get('operations',{}).items():
        require(key in display['operations'],f'Unknown presentation request {key}.','authoring.presentation')
        fields(value, [], ['label','accessibleLabel','aliases'])
        display['operations'][key].update(copy.deepcopy(value))
    candidate={**presentation,'display':display}
    validate_movement_presentation(candidate)
    presentation['display']=display
