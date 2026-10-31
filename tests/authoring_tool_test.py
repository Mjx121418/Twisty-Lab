#!/usr/bin/env python3
"""Independent authoring consumer, provenance and publication boundary checks."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.authoring.build import file_hashes
from tools.authoring.legacy import publication_entries
from tools.authoring.transaction import publish


class AuthoringConsumer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary=tempfile.TemporaryDirectory(prefix='twisty-author-consumer-')
        cls.host=Path(cls.temporary.name)/'sdk'
        subprocess.run([sys.executable,'-B','-m','tools.authoring.package','--tools-dir',str(ROOT/'build/native'),'--output',str(cls.host)],
                       cwd=ROOT,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        cls.cli=cls.host/'tools/authoring/twisty-author'
        cls.environment={key:value for key,value in os.environ.items() if not key.startswith('TWISTY_AUTHORING_') and key!='PYTHONPATH'}
        cls.environment['PYTHONDONTWRITEBYTECODE']='1'

    @classmethod
    def tearDownClass(cls): cls.temporary.cleanup()

    def setUp(self):
        self.directory=Path(tempfile.mkdtemp(prefix='project-',dir=self.host))
        inputs=self.directory/'inputs';inputs.mkdir()
        views=[]
        for name in ('package.json','cube-euclidean.json','cube-spherical.json'):
            shutil.copyfile(ROOT/'packages/cube3'/name,inputs/name)
            if name!='package.json': views.append({'id':Path(name).stem,'path':'inputs/'+name})
        self.project=self.directory/'project.json'
        self.document={'kind':'twisty-authoring-project','projectVersion':1,'id':'cube3',
                       'producer':{'type':'source','package':'inputs/package.json','realizations':views},'output':'output'}
        self.save()
        self.output=self.directory/'output'

    def save(self): self.project.write_text(json.dumps(self.document)+'\n')

    def command(self,command,*args,success=True):
        result=subprocess.run([sys.executable,str(self.cli),command,str(self.project),'--tools-dir',str(self.host/'bin'),'--json',*map(str,args)],
                              cwd=self.host,env=self.environment,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        self.assertEqual(result.returncode==0,success,result.stdout+result.stderr)
        return json.loads(result.stdout if success else result.stderr)

    def test_detached_reproducible_read_only_and_native_validation(self):
        self.assertTrue(self.command('build')['built'])
        before=file_hashes(self.output);receipt=(self.output/'build-manifest.json').read_bytes()
        snapshot={str(path):path.stat().st_mtime_ns for path in self.host.rglob('*')}
        for command in ('inspect','check','validate'): self.command(command)
        self.command('check','--files-only')
        self.assertEqual(snapshot,{str(path):path.stat().st_mtime_ns for path in self.host.rglob('*')})
        self.assertFalse(self.command('build')['built'])
        self.command('build','--rebuild')
        self.assertEqual(before,file_hashes(self.output))
        self.assertEqual(receipt,(self.output/'build-manifest.json').read_bytes())
        self.assertFalse(list(self.host.rglob('__pycache__')))
        relocated=self.host/('relocated-'+self.directory.name)
        shutil.copytree(self.directory,relocated)
        self.project=relocated/'project.json'
        self.command('check')
        self.assertFalse(self.command('build')['built'])

    def test_export_and_validation_failures_preserve_published_tree(self):
        self.command('build');before=file_hashes(self.output)
        receipt=(self.output/'build-manifest.json').read_bytes()
        failing=self.directory/'failing-tool'
        failing.write_text('#!'+sys.executable+'\nimport sys\nprint("injected native failure",file=sys.stderr)\nsys.exit(1)\n');failing.chmod(0o755)
        for flag in ('--twisty-artifact-export','--twisty-artifact-validate'):
            error=self.command('build','--rebuild',flag,failing,success=False)
            self.assertEqual(error['code'],'authoring.native')
            self.assertEqual(before,file_hashes(self.output))
            self.assertEqual(receipt,(self.output/'build-manifest.json').read_bytes())
        view=self.directory/'inputs/cube-euclidean.json'
        invalid=json.loads(view.read_text());invalid['compatibleDefinitionDigest']='0'*64
        view.write_text(json.dumps(invalid))
        self.assertEqual(self.command('build','--rebuild',success=False)['code'],'authoring.native')
        self.assertEqual(before,file_hashes(self.output))
        self.assertEqual(receipt,(self.output/'build-manifest.json').read_bytes())
        self.document['producer']={'type':'preset','model':'cube3'};self.save()
        error=self.command('build','--rebuild','--twisty',failing,success=False)
        self.assertEqual(error['code'],'authoring.generator')
        self.assertEqual(before,file_hashes(self.output))
        self.assertEqual(receipt,(self.output/'build-manifest.json').read_bytes())
        self.assertFalse(list(self.directory.glob('.twisty-stage-*')))
        self.assertFalse(list(self.directory.glob('*.authoring-lock')))

    def test_stale_inputs_and_corrupt_payload(self):
        self.command('build')
        path=self.directory/'inputs/package.json';path.write_text(path.read_text()+'\n')
        self.assertEqual(self.command('check',success=False)['code'],'authoring.stale')
        self.command('validate')
        self.assertTrue(self.command('build')['built'])
        payload=self.output/'realizations/cube-euclidean/geometry.json';payload.write_text('{}')
        self.assertEqual(self.command('validate',success=False)['code'],'authoring.stale')
        self.command('build');self.command('check')
        (self.output/'user-file.txt').write_text('owned by user')
        self.command('build','--rebuild',success=False)
        self.assertEqual((self.output/'user-file.txt').read_text(),'owned by user')

    def test_selected_preset_and_bounded_dependencies(self):
        self.document['producer']={'type':'preset','model':'cube3'};self.save()
        result=self.command('build')['build']
        self.assertEqual(set(result['realizations'][0]),{'id','backend','realizationDigest','parts','space'})
        self.assertFalse(any('melty' in key or 'gear_' in key for key in result['inputs']))
        self.assertEqual(set(result['tools']),{'twisty','twisty_language','twisty_artifact_export','twisty_artifact_validate'})
        for path in (self.output/'legacy').iterdir():
            self.assertEqual(path.read_bytes(),(ROOT/'packages/cube3'/path.name).read_bytes())
        unrelated=self.host/'tools/authoring/data/melty-a-witnesses.json'
        original=unrelated.read_bytes()
        try:
            unrelated.write_bytes(original+b'\n');self.command('check')
        finally: unrelated.write_bytes(original)
        model=self.host/'tools/authoring/models/cube_packages.py';original=model.read_bytes()
        try:
            model.write_bytes(original+b'\n')
            self.assertEqual(self.command('check',success=False)['code'],'authoring.stale')
            self.assertTrue(self.command('build')['built'])
        finally: model.write_bytes(original)

    def test_invalid_manifest_and_symlink_destination(self):
        self.document['projectVersion']=True;self.save();self.command('inspect',success=False)
        self.document['projectVersion']=1;self.save()
        target=self.directory/'user-folder';target.mkdir();(target/'keep').write_text('keep')
        self.output.symlink_to(target,target_is_directory=True)
        self.command('build',success=False)
        self.assertEqual((target/'keep').read_text(),'keep')


class PublicationRollback(unittest.TestCase):
    def test_reference_publication_preserves_package_documentation(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent=Path(temporary);workspace=parent/'workspace';stage=parent/'stage'
            inventory={'puzzles':[{'id':'example','realizations':['geometry.json'],'files':[]}]}
            for tree in (workspace,stage):
                folder=tree/'packages/example';folder.mkdir(parents=True)
                for name in ('package.json','puzzle.pzl.json','bindings.json','geometry.json'):
                    (folder/name).write_text('new' if tree==stage else 'old')
                (tree/'packages/build-manifest.json').write_text('receipt')
            documentation=workspace/'packages/example/README.md';documentation.write_text('keep me')
            entries=publication_entries(workspace,stage,inventory)
            self.assertEqual(entries[-1][1],workspace/'packages/build-manifest.json')
            publish(entries,parent)
            self.assertEqual(documentation.read_text(),'keep me')
            self.assertEqual((workspace/'packages/example/package.json').read_text(),'new')

    def test_second_directory_failure_restores_both_outputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent=Path(temporary);entries=[]
            for name in ('one','two'):
                staged=parent/(name+'-stage');destination=parent/name
                staged.mkdir();destination.mkdir()
                (staged/'file').write_text('new');(destination/'file').write_text('old')
                entries.append((staged,destination))
            real_replace=os.replace
            def replace(source,destination):
                if source==entries[1][0]: raise OSError('injected publication failure')
                return real_replace(source,destination)
            with patch('tools.authoring.transaction.os.replace',side_effect=replace):
                with self.assertRaises(OSError): publish(entries,parent)
            for _,destination in entries: self.assertEqual((destination/'file').read_text(),'old')


if __name__=='__main__': unittest.main(verbosity=2)
