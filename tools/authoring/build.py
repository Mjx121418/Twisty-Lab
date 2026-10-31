"""Staging, native validation, receipts and publication for one project."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from .environment import PACKAGE
from .project import AuthoringError, fields, read_json, require, sha_file
from .transaction import lock, publish

RECEIPT = 'build-manifest.json'


def write_json(path, value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':'))+'\n',encoding='utf-8')


def resolve_tools(project, configured, names=None):
    result={}
    for name in project.tools() if names is None else names:
        value=configured.get(name) or shutil.which(name)
        require(value is not None, f'Missing native tool {name}; supply a tool path.', 'authoring.tool')
        path=Path(value).resolve()
        require(path.is_file() and os.access(path,os.X_OK), f'Native tool is not executable: {path}.', 'authoring.tool')
        result[name]=path
    return result


def tool_hashes(tools): return {name:sha_file(path) for name,path in sorted(tools.items())}


def run_json(command):
    result=subprocess.run([str(item) for item in command],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
    if result.returncode:
        raise AuthoringError('authoring.native', result.stderr[-6000:] or f'Native command failed: {command[0]}')
    try: return json.loads(result.stdout)
    except (ValueError,RecursionError) as error: raise AuthoringError('authoring.native', 'Native tool returned invalid JSON.') from error


def file_hashes(directory):
    result={}
    for path in sorted(directory.rglob('*')):
        require(not path.is_symlink(), f'Output contains a symlink: {path}.')
        if path.is_file() and path != directory/RECEIPT: result[path.relative_to(directory).as_posix()]=sha_file(path)
    return result


def receipt(directory, project_id=None):
    value=read_json(directory/RECEIPT)
    fields(value,['kind','buildVersion','projectId','inputs','tools','outputs','executionDigest','interpretationDigest','realizations'])
    require(value['kind']=='twisty-authoring-build' and type(value['buildVersion']) is int and value['buildVersion']==1, 'Unsupported authoring build receipt.')
    if project_id is not None: require(value['projectId']==project_id,'Output belongs to another authoring project.')
    for group in ('inputs','tools','outputs'):
        require(isinstance(value[group],dict) and value[group], f'Receipt must declare its {group}.')
        for name,digest in value[group].items():
            require(isinstance(name,str) and isinstance(digest,str) and len(digest)==64 and all(c in '0123456789abcdef' for c in digest),'Invalid receipt content hash.')
    require(isinstance(value['realizations'],list) and 0<len(value['realizations'])<=64,'Receipt must declare 1–64 realizations.')
    seen=set()
    from .project import identifier
    for view in value['realizations']:
        fields(view,['id','backend','realizationDigest','parts','space'])
        identifier(view['id'])
        fields(view['backend'],['id','version'])
        require(isinstance(view['backend']['id'],str) and type(view['backend']['version']) is int and view['backend']['version']>0 and
                isinstance(view['space'],dict) and type(view['parts']) is int and view['parts']>0,
                'Invalid receipt backend, space or part count.')
        require(view['id'] not in seen,'Duplicate receipt realization ID.')
        seen.add(view['id'])
    for name,digest in value['outputs'].items():
        require(isinstance(name,str) and name and not name.startswith('/') and '\\' not in name and '..' not in Path(name).parts and name!=RECEIPT,
                'Invalid receipt output path.')
        require(isinstance(digest,str) and len(digest)==64 and all(c in '0123456789abcdef' for c in digest),'Invalid output content hash.')
    return value


def validate_output(directory, tools, recorded=None):
    recorded=recorded or receipt(directory)
    require(file_hashes(directory)==recorded['outputs'],'Generated output bytes or file inventory differ from the receipt.','authoring.stale')
    results=[]
    for view in recorded['realizations']:
        identifier=view['id']
        require(isinstance(identifier,str) and identifier and Path(identifier).name==identifier and identifier not in ('.','..'),'Invalid receipt realization ID.')
        folder=directory/'realizations'/identifier
        result=run_json([tools['twisty_artifact_validate'],directory/'abstract.json',directory/'interpretation.json',folder/'realization.json',folder/'geometry.json'])
        require(result.get('status')=='Valid' and result.get('executionDigest')==recorded['executionDigest'] and
                result.get('interpretationDigest')==recorded['interpretationDigest'] and result.get('realizationDigest')==view['realizationDigest'],
                'Native realization identity differs from the receipt.','authoring.native')
        results.append(result)
    require(results,'No realizations were declared in the build receipt.')
    return results


def check(project, output, configured, files_only=False):
    tools=resolve_tools(project,configured)
    recorded=receipt(output,project.id)
    require(project.hashes()==recorded['inputs'],'Authoring project inputs or producer code changed; rebuild this project.','authoring.stale')
    require(tool_hashes(tools)==recorded['tools'],'Native tool bytes changed; rebuild this project.','authoring.stale')
    require(file_hashes(output)==recorded['outputs'],'Generated output bytes or file inventory changed.','authoring.stale')
    if not files_only: validate_output(output,tools,recorded)
    return recorded


def generate_preset(project, working, tools):
    environment=os.environ.copy()
    environment.update(PYTHONDONTWRITEBYTECODE='1',TWISTY_AUTHORING_WORKSPACE=str(working),TWISTY_AUTHORING_PROJECT=project.id,
                       TWISTY_AUTHORING_TOOLS=json.dumps({key:str(value) for key,value in tools.items()}))
    # Package location configures Python imports only; generators use the staged
    # workspace and explicit binaries for every input/output host operation.
    environment['PYTHONPATH']=str(PACKAGE.parents[1])+os.pathsep+environment.get('PYTHONPATH','')
    for target,source in project.producer.get('inputs',{}).items():
        destination=working/target;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(project.input(source),destination)
    (working/'packages'/project.id).mkdir(parents=True,exist_ok=True)
    for module in project.spec['generators']:
        result=subprocess.run([sys.executable,'-m','tools.authoring.models.'+module,'--write'],cwd=working,env=environment,
                              stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
        if result.returncode: raise AuthoringError('authoring.generator',result.stderr[-6000:] or result.stdout[-6000:])
    return working/'packages'/project.id


def legacy_inputs(project, working, tools):
    if project.kind=='preset':
        folder=generate_preset(project,working,tools)
        actual={path.name for path in folder.iterdir() if path.is_file()}
        require(actual==set(project.spec['files']),f'Preset output inventory differs: {sorted(actual.symmetric_difference(project.spec["files"]))}.')
        return folder,[(Path(name).stem,folder/name) for name in project.spec['realizations']]
    folder=working/'legacy';folder.mkdir(parents=True)
    document=read_json(project.source)
    require(isinstance(document,dict) and document.get('kind')=='relational-package' and 'source' in document and 'catalog' in document,'Source projects require an explicit relational package.')
    shutil.copyfile(project.source,folder/'package.json')
    write_json(folder/'puzzle.pzl.json',document['source']);write_json(folder/'bindings.json',document['catalog'])
    views=[]
    for key,path in project.views:
        read_json(path)
        destination=folder/'realizations'/(key+'.json');destination.parent.mkdir(exist_ok=True)
        shutil.copyfile(path,destination);views.append((key,destination))
    return folder,views


def build(project, output, configured, rebuild=False):
    # Resolve the parent, but reject a final symlink before following it.
    require(not output.is_symlink(),'Build output cannot be a symlink.')
    output=output.parent.resolve()/output.name
    require(output.name not in ('','.','..'),'Choose a dedicated output directory.')
    for source in project.dependencies().values():
        require(not source.resolve().is_relative_to(output), 'Build output cannot contain project inputs or producer code.')
    output.parent.mkdir(parents=True,exist_ok=True)
    with lock(output.parent/('.'+output.name+'.authoring-lock')):
        if output.exists():
            previous=receipt(output,project.id)
            require(set(file_hashes(output))==set(previous['outputs']),'Output contains unowned files; choose a dedicated build directory.')
            if not rebuild:
                try: return check(project,output,configured,files_only=True),False
                except AuthoringError as error:
                    if error.code!='authoring.stale': raise
        tools=resolve_tools(project,configured);before=project.hashes();binaries=tool_hashes(tools)
        with tempfile.TemporaryDirectory(prefix='.twisty-stage-',dir=output.parent) as temporary:
            stage=Path(temporary);working=stage/'work';working.mkdir()
            candidate=stage/'candidate';candidate.mkdir()
            legacy,views=legacy_inputs(project,working,tools)
            shutil.copytree(legacy,candidate/'legacy')
            common=None;realizations=[]
            for key,path in views:
                bundle=run_json([tools['twisty_artifact_export'],legacy/'package.json',path])
                fields(bundle,['abstract','interpretation','realization','payload'])
                require(isinstance(bundle['payload'],str),'Exporter must return exact payload text.')
                current=(bundle['abstract'],bundle['interpretation'])
                if common is None:
                    common=current;write_json(candidate/'abstract.json',common[0]);write_json(candidate/'interpretation.json',common[1])
                else: require(current==common,'Realizations disagree on their shared execution/correspondence.','authoring.native')
                directory=candidate/'realizations'/key;write_json(directory/'realization.json',bundle['realization'])
                (directory/'geometry.json').write_bytes(bundle['payload'].encode('utf-8'))
                verified=run_json([tools['twisty_artifact_validate'],candidate/'abstract.json',candidate/'interpretation.json',directory/'realization.json',directory/'geometry.json'])
                require(verified.get('status')=='Valid' and verified.get('executionDigest')==common[0]['executionDigest'] and
                        verified.get('interpretationDigest')==common[1]['interpretationDigest'] and
                        verified.get('realizationDigest')==bundle['realization']['realizationDigest'],
                        'Native artifact validation or identity check failed.','authoring.native')
                realizations.append({'id':key,'backend':bundle['realization']['backend'],'realizationDigest':verified['realizationDigest'],
                                     'parts':verified['parts'],'space':verified['space']})
            require(common is not None,'A project needs at least one realization.')
            require(project.hashes()==before and tool_hashes(tools)==binaries,'Authoring inputs or native tools changed during generation.','authoring.changed')
            recorded={'kind':'twisty-authoring-build','buildVersion':1,'projectId':project.id,'inputs':before,'tools':binaries,
                      'outputs':file_hashes(candidate),'executionDigest':common[0]['executionDigest'],
                      'interpretationDigest':common[1]['interpretationDigest'],'realizations':realizations}
            write_json(candidate/RECEIPT,recorded)
            publish([(candidate,output)],output.parent)
            return recorded,True
