"""Optional presentation overrides. These never enter semantic contracts."""
import copy
import math
from .project import fields, require


def apply_presentation(bundle, override=None):
    presentation = bundle['presentation']
    display = copy.deepcopy(presentation['display'])
    if override is not None:
        fields(override, ['version'], ['operations','familyOrder','instructions','defaultAlgorithm'])
        require(type(override['version']) is int and override['version']==1,'Unsupported presentation override version.')
        for key in ('familyOrder','instructions','defaultAlgorithm'):
            if key in override: display[key]=copy.deepcopy(override[key])
        require(isinstance(override.get('operations',{}),dict),'Expected operation presentation overrides.')
        for key,value in override.get('operations',{}).items():
            require(key in display['operations'],f'Unknown presentation request {key}.','authoring.presentation')
            fields(value, [], ['label','accessibleLabel','aliases'])
            display['operations'][key].update(copy.deepcopy(value))
    requests={op['id'] for op in presentation['operations']}
    families={op['family'] for op in presentation['operations']}
    require(set(display['operations'])==requests,'Presentation must cover every request.','authoring.presentation')
    order=display['familyOrder']
    require(isinstance(order,list) and all(isinstance(key,str) for key in order) and len(order)==len(set(order)) and set(order)==families,
            'Presentation family order must list each family exactly once.','authoring.presentation')
    aliases=set(requests)
    for key,value in display['operations'].items():
        for name in ('label','accessibleLabel'):
            require(isinstance(value[name],str) and 0<len(value[name])<=256,'Display labels must contain 1–256 characters.','authoring.presentation')
        require(isinstance(value['aliases'],list) and len(value['aliases'])<=32,'Expected at most 32 aliases.','authoring.presentation')
        for alias in value['aliases']:
            require(isinstance(alias,str) and 0<len(alias)<=256 and alias not in aliases,'Aliases must be unique and cannot shadow request IDs.','authoring.presentation')
            aliases.add(alias)
        require(all(isinstance(angle,(int,float)) and not isinstance(angle,bool) and math.isfinite(angle) for angle in value.get('angleDegrees',[])),
                'Presentation angles must be finite.','authoring.presentation')
    for key in ('instructions','defaultAlgorithm'):
        require(isinstance(display[key],str) and len(display[key])<=4096,'Invalid presentation text.','authoring.presentation')
    presentation['display']=display
