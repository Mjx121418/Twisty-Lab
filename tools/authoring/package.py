"""Assemble a standalone authoring tool, explicit native hosts and source example."""
import argparse
from pathlib import Path
import shutil
import tempfile
from .environment import PACKAGE, TOOL_NAMES
from .project import read_json, require
from .transaction import publish
from .build import write_json


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tools-dir',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    output=args.output.absolute()
    require(not output.is_symlink(),'Package output cannot be a symlink.')
    source_root=PACKAGE.parents[1]
    require(output != source_root and not source_root.is_relative_to(output),'Package output cannot contain the source tree.')
    require(not output.is_relative_to(PACKAGE),'Package output cannot be inside the authoring source package.')
    if output.exists():
        require((output/'authoring-package.json').is_file(),'Choose an empty or previously packaged authoring output directory.')
        require(read_json(output/'authoring-package.json')=={'kind':'twisty-authoring-sdk','packageVersion':1},'Unrecognized authoring package output.')
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.authoring-sdk-',dir=output.parent) as temporary:
        candidate=Path(temporary)/'candidate';candidate.mkdir()
        destination=candidate/'tools/authoring'
        shutil.copytree(PACKAGE,destination,ignore=shutil.ignore_patterns('__pycache__','projects','reference-build.mjs'))
        shutil.copyfile(source_root/'tools/__init__.py',candidate/'tools/__init__.py')
        (candidate/'bin').mkdir()
        for name in TOOL_NAMES: shutil.copy2(args.tools_dir/name,candidate/'bin'/name)
        inputs=candidate/'examples/source/inputs';inputs.mkdir(parents=True)
        views=[]
        for name in ('package.json','cube-euclidean.json','cube-port-diagram.json','cube-spherical.json'):
            shutil.copyfile(source_root/'packages/cube3'/name,inputs/name)
            if name!='package.json': views.append({'id':Path(name).stem,'path':'inputs/'+name})
        write_json(inputs.parent/'project.json',{'kind':'twisty-authoring-project','projectVersion':1,'id':'cube3',
                                               'producer':{'type':'source','package':'inputs/package.json','realizations':views},'output':'output'})
        example=candidate/'examples/preset';example.mkdir(parents=True)
        write_json(example/'project.json',{'kind':'twisty-authoring-project','projectVersion':1,'id':'cube3',
                                          'producer':{'type':'preset','model':'cube3'},'output':'output'})
        shutil.copyfile(source_root/'docs/authoring-tool.md',candidate/'README.md')
        write_json(candidate/'authoring-package.json',{'kind':'twisty-authoring-sdk','packageVersion':1})
        publish([(candidate,output)],output.parent)
    print(f'Standalone authoring tool: {output}')


if __name__=='__main__': main()
