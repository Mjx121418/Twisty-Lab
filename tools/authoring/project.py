"""Versioned project descriptions and bounded producer dependencies."""
import ast
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
from .environment import PACKAGE


class AuthoringError(Exception):
    def __init__(self, code, message):
        self.code = code
        super().__init__(message)


def require(condition, message, code='authoring.invalid'):
    if not condition:
        raise AuthoringError(code, message)


def sha_file(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def read_json(path):
    def object_pairs(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f'Duplicate JSON key {key!r} in {path}.')
            result[key] = value
        return result
    try:
        require(Path(path).stat().st_size <= 64*1024*1024, f'JSON input exceeds 64 MiB: {path}.')
        return json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=object_pairs,
                          parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))
    except (OSError, ValueError, RecursionError) as error:
        raise AuthoringError('authoring.input', f'Cannot read JSON {path}: {error}') from error


def fields(value, required, optional=()):
    require(isinstance(value, dict) and set(required) <= value.keys(), 'Project object is missing required fields.')
    require(not(value.keys()-set(required)-set(optional)), 'Unknown project fields: '+', '.join(sorted(value.keys()-set(required)-set(optional))))


def identifier(value):
    require(isinstance(value, str) and re.fullmatch(r'[a-z0-9][a-z0-9._-]{0,127}', value) and value not in ('.','..'), 'Expected a lowercase project/view identifier.')
    return value


def relative(value):
    require(isinstance(value, str) and value and '\\' not in value and not PurePosixPath(value).is_absolute() and ':' not in value,
            'Expected an explicit relative input/output path.')
    return value


def stage_path(value):
    relative(value)
    require(str(PurePosixPath(value)) == value and '..' not in PurePosixPath(value).parts and '.' not in PurePosixPath(value).parts,
            'Staged input paths must be normalized and remain inside the working tree.')
    return value


class Project:
    def __init__(self, filename):
        self.path = Path(filename).resolve()
        require(self.path.stat().st_size <= 1024*1024, 'Project manifest exceeds 1 MiB.')
        self.document = read_json(self.path)
        fields(self.document, ['kind','projectVersion','id','producer'], ['output','includeLegacy'])
        require(type(self.document.get('includeLegacy',True)) is bool,'includeLegacy must be a boolean.')
        require(self.document['kind']=='twisty-authoring-project' and type(self.document['projectVersion']) is int and self.document['projectVersion']==1, 'Unsupported authoring project kind/version.')
        self.id = identifier(self.document['id'])
        self.producer = self.document['producer']
        require(isinstance(self.producer, dict), 'Expected a producer object.')
        self.kind = self.producer.get('type')
        if self.kind == 'source':
            fields(self.producer, ['type','package','realizations'])
            self.source = self.input(self.producer['package'])
            self.views = []
            require(isinstance(self.producer['realizations'],list) and 0<len(self.producer['realizations'])<=64, 'Declare 1–64 realizations.')
            for view in self.producer['realizations']:
                fields(view,['id','path'])
                self.views.append((identifier(view['id']),self.input(view['path'])))
            require(len({key for key,_ in self.views})==len(self.views), 'Duplicate realization identity.')
            self.spec = None
        elif self.kind == 'preset':
            fields(self.producer,['type','model'],['inputs'])
            model = identifier(self.producer['model'])
            catalog = read_json(PACKAGE/'presets.json')
            require(model in catalog, f'Unknown reference preset {model}.')
            self.spec = catalog[model]
            require(self.id == model, 'Reference preset project IDs must match their selected model.')
            supplied = self.producer.get('inputs',{})
            require(isinstance(supplied,dict) and set(supplied)==set(self.spec['inputs']), 'Declare exactly the selected preset input files.')
            for staged, source in supplied.items(): stage_path(staged); self.input(source)
            self.source = None; self.views = []
        else:
            raise AuthoringError('authoring.unsupported', 'Producer type must be source or preset.')
        self.output = Path(os.path.abspath(self.path.parent/relative(self.document.get('output','build/'+self.id))))

    def input(self, value):
        return (self.path.parent/relative(value)).resolve()

    def tools(self):
        names = {'twisty_artifact_export','twisty_artifact_validate'}
        if self.spec: names.update(self.spec['tools'])
        return sorted(names)

    def modules(self):
        pending = list(self.spec['generators']) if self.spec else []
        result = set()
        while pending:
            name = pending.pop()
            if name in result: continue
            path = PACKAGE/'models'/f'{name}.py'
            require(path.is_file(), f'Missing model producer {name}.')
            result.add(name)
            for node in ast.walk(ast.parse(path.read_text(encoding='utf-8'))):
                if isinstance(node, ast.ImportFrom) and node.level==1:
                    if node.module: pending.append(node.module.split('.')[0])
                    else: pending.extend(alias.name for alias in node.names)
        return sorted(result)

    def dependencies(self):
        inputs = {'project': self.path}
        if self.kind=='source':
            inputs['source:'+self.producer['package']] = self.source
            for view, (key,path) in zip(self.producer['realizations'],self.views): inputs['view:'+key+':'+view['path']] = path
        else:
            for destination,source in self.producer.get('inputs',{}).items(): inputs['seed:'+destination] = self.input(source)
            for name in self.spec['data']: inputs['tool:data/'+name] = PACKAGE/'data'/name
            # Only the selected reference descriptor enters this project's identity.
        for name in ('__init__.py','environment.py','project.py','build.py','transaction.py','cli.py','twisty-author'):
            inputs['tool:'+name] = PACKAGE/name
        inputs['tool:project.schema.json'] = PACKAGE/'project.schema.json'
        inputs['tool:models/__init__.py'] = PACKAGE/'models/__init__.py'
        for name in self.modules(): inputs['tool:models/'+name+'.py'] = PACKAGE/'models'/f'{name}.py'
        return inputs

    def hashes(self):
        result = {key:sha_file(path) for key,path in sorted(self.dependencies().items())}
        if self.spec:
            result['tool:preset-description'] = hashlib.sha256(json.dumps(self.spec,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        return result
