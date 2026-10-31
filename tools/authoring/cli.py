"""Command-line host for versioned, independently buildable authoring projects."""
import argparse
import json
import os
from pathlib import Path
import sys
from .build import build, check, receipt, resolve_tools, validate_output
from .environment import TOOL_NAMES
from .project import AuthoringError, Project


def main(argv=None):
    parser=argparse.ArgumentParser(description='Build, check, inspect and validate portable puzzle authoring projects.')
    commands=parser.add_subparsers(dest='command',required=True)
    for name in ('build','check','inspect','validate'):
        command=commands.add_parser(name)
        command.add_argument('project',type=Path,help='Version 1 authoring project JSON')
        command.add_argument('--output',type=Path,help='Override the dedicated output directory (relative to current directory)')
        command.add_argument('--tools-dir',type=Path,help='Directory containing explicit native executables')
        for tool in TOOL_NAMES: command.add_argument('--'+tool.replace('_','-'),type=Path,dest=tool)
        command.add_argument('--json',action='store_true',help='Emit a structured result')
        if name=='build': command.add_argument('--rebuild',action='store_true',help='Force deterministic regeneration')
        if name=='check': command.add_argument('--files-only',action='store_true',help='Check provenance and bytes without executing native tools')
    args=parser.parse_args(argv)
    try:
        project=Project(args.project)
        output=Path(os.path.abspath(args.output)) if args.output else project.output
        configured={name:args.tools_dir/name for name in TOOL_NAMES} if args.tools_dir else {}
        configured.update({name:getattr(args,name) for name in TOOL_NAMES if getattr(args,name) is not None})
        if args.command=='inspect':
            result={'status':'Valid','projectId':project.id,'producer':project.kind,'output':str(output),
                    'tools':project.tools(),'inputs':{key:str(value) for key,value in project.dependencies().items()}}
            if (output/'build-manifest.json').is_file(): result['build']=receipt(output,project.id)
        elif args.command=='build':
            recorded,built=build(project,output,configured,args.rebuild)
            result={'status':'Valid','projectId':project.id,'built':built,'output':str(output),'build':recorded}
        elif args.command=='check':
            recorded=check(project,output,configured,args.files_only)
            result={'status':'Valid','projectId':project.id,'build':recorded}
        else:
            recorded=receipt(output,project.id)
            tools=resolve_tools(project,configured,['twisty_artifact_validate'])
            result={'status':'Valid','projectId':project.id,'realizations':validate_output(output,tools,recorded)}
        if args.json: print(json.dumps(result,sort_keys=True,separators=(',',':')))
        else: print(f'{args.command}: {project.id}: Valid'+(f' ({"built" if result["built"] else "up to date"})' if 'built' in result else ''))
        return 0
    except (AuthoringError,OSError,ValueError,RuntimeError) as error:
        result={'status':'Invalid','code':getattr(error,'code','authoring.io'),'message':str(error)}
        print(json.dumps(result) if args.json else f'{result["code"]}: {error}',file=sys.stderr)
        return 1
