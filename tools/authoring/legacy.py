"""Reference-repository compatibility host; project authoring stays repository independent."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from .build import run_json
from .environment import PACKAGE, TOOL_NAMES
from .project import AuthoringError, read_json, require
from .transaction import publish


def validate(workspace, tools, update_fixtures=False):
    inventory=read_json(workspace/'scripts/puzzle-inventory.json')
    pins={};count=0
    for puzzle in inventory['puzzles']:
        source=workspace/'packages'/puzzle['id']/'package.json'
        for name in puzzle['realizations']:
            result=run_json([tools['twisty_artifact_export'],'--validate-source',source,source.parent/name])
            require(result.get('status')=='Valid','Native geometry validation failed.','authoring.native')
            digest=result['definitionDigest']
            require(puzzle['id'] not in pins or pins[puzzle['id']]==digest,'Realizations disagree on execution identity.')
            pins[puzzle['id']]=digest;count+=1
    fixture_path=workspace/'tests/fixtures/core.json'
    fixtures=read_json(fixture_path)
    for case in fixtures['cases']:
        digest=pins.get(Path(case['source']).parent.name)
        require(digest is not None,f'Unknown fixture source {case["source"]}.')
        if update_fixtures: case['definitionDigest']=digest
        else: require(case['definitionDigest']==digest,f'Fixture {case["name"]} has a changed semantic pin; review before --update-fixtures.')
    if update_fixtures:
        fixture_path.write_text(json.dumps(fixtures,indent=2)+'\n',encoding='utf-8')
    return {'puzzles':len(pins),'realizations':count}


def stage_reference(workspace, stage, tools, update_fixtures=False):
    inventory=read_json(workspace/'scripts/puzzle-inventory.json')
    presets=read_json(PACKAGE/'presets.json')
    require(set(presets)=={puzzle['id'] for puzzle in inventory['puzzles']},'Reference inventory and authoring presets disagree.')
    for name in ('scripts','tests/fixtures'): (stage/name).mkdir(parents=True,exist_ok=True)
    shutil.copyfile(workspace/'scripts/puzzle-inventory.json',stage/'scripts/puzzle-inventory.json')
    shutil.copyfile(workspace/'tests/fixtures/core.json',stage/'tests/fixtures/core.json')
    shutil.copytree(workspace/'schemas',stage/'schemas')
    seeds=set()
    for spec in presets.values(): seeds.update(spec['inputs'])
    for name in sorted(seeds):
        destination=stage/name;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(workspace/name,destination)
    for name in presets: (stage/'packages'/name).mkdir(parents=True,exist_ok=True)
    environment=os.environ.copy()
    environment.pop('TWISTY_AUTHORING_PROJECT',None)
    environment.update(PYTHONDONTWRITEBYTECODE='1',TWISTY_AUTHORING_WORKSPACE=str(stage),
                       TWISTY_AUTHORING_TOOLS=json.dumps({name:str(path) for name,path in tools.items()}))
    environment['PYTHONPATH']=str(PACKAGE.parents[1])+os.pathsep+environment.get('PYTHONPATH','')
    for script in inventory['generators']:
        module=Path(script).stem.replace('-','_')
        print(f'Authoring {module}',flush=True)
        subprocess.run([sys.executable,'-m','tools.authoring.models.'+module,'--write'],cwd=stage,env=environment,check=True)
    for name,spec in presets.items():
        actual={path.name for path in (stage/'packages'/name).iterdir()}
        require(actual==set(spec['files']),f'Reference output inventory differs for {name}: {actual.symmetric_difference(spec["files"])}.')
    result=validate(stage,tools,update_fixtures)
    print(f'Native validation: {result["puzzles"]} puzzles, {result["realizations"]} realizations.',flush=True)


def publication_entries(workspace, stage, inventory, update_fixtures=False):
    # The repository owns declared generated files, not entire package folders.
    # Keep READMEs and other accompanying files through successful publication.
    entries=[]
    for puzzle in inventory['puzzles']:
        folder=Path('packages')/puzzle['id']
        (workspace/folder).mkdir(parents=True,exist_ok=True)
        for name in ['puzzle.pzl.json','bindings.json','package.json',*puzzle['realizations'],*puzzle['files']]:
            entries.append((stage/folder/name,workspace/folder/name))
    if update_fixtures: entries.append((stage/'tests/fixtures/core.json',workspace/'tests/fixtures/core.json'))
    entries.append((stage/'packages/build-manifest.json',workspace/'packages/build-manifest.json'))
    return entries


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['stage','publish','validate'])
    parser.add_argument('--workspace',required=True,type=Path)
    parser.add_argument('--stage',type=Path)
    parser.add_argument('--tools-dir',type=Path)
    parser.add_argument('--update-fixtures',action='store_true')
    args=parser.parse_args()
    workspace=args.workspace.resolve()
    try:
        if args.command=='publish':
            require(args.stage is not None,'Specify staging tree.')
            inventory=read_json(workspace/'scripts/puzzle-inventory.json');stage=args.stage.resolve()
            entries=publication_entries(workspace,stage,inventory,args.update_fixtures)
            publish(entries,workspace/'build')
        else:
            require(args.tools_dir is not None,'Specify native tools directory.')
            tools={name:(args.tools_dir/name).resolve() for name in TOOL_NAMES}
            if args.command=='stage':
                require(args.stage is not None and args.stage.is_dir(),'Specify an empty staging directory.')
                require(not any(args.stage.iterdir()),'Staging directory must be empty.')
                stage_reference(workspace,args.stage.resolve(),tools,args.update_fixtures)
            else:
                require(not args.update_fixtures,'Validation is read-only.')
                print(json.dumps(validate(workspace,tools)))
        return 0
    except (AuthoringError,OSError,ValueError,subprocess.CalledProcessError) as error:
        print(f'{getattr(error,"code","authoring.reference")}: {error}',file=sys.stderr)
        return 1


if __name__=='__main__': raise SystemExit(main())
