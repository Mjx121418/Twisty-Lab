"""Reject unregistered rule changes and preserve the immutable compiler oracle."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
EXECUTABLE=Path(sys.argv.pop(1)).resolve()


class RuleRevisionPolicy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(prefix='twisty-reference-policy-')
        cls.workspace=Path(cls.directory.name)
        cls.package=cls.workspace/'packages/cube3/package.json'
        cls.package.parent.mkdir(parents=True)
        cls.original=json.loads((ROOT/'packages/cube3/package.json').read_text())
        def record(value):
            path=cls.workspace/'record.json';path.write_text(json.dumps(value))
            return json.loads(subprocess.check_output([str(EXECUTABLE),'--record',str(path)],cwd=cls.workspace,text=True))
        cls.oracle=record(cls.original);cls.oracle['name']='cube3'
        cls.revised=json.loads(json.dumps(cls.original));cls.revised['source']['goal']=False
        recorded=record(cls.revised)
        cls.revision={'commit':'1'*40,'legacyDefinitionDigest':cls.oracle['digest'],
                      'packageDigest':recorded['packageDigest'],'definitionDigest':recorded['digest']}
        cls.oracle_path=cls.workspace/'oracle.json';cls.revisions_path=cls.workspace/'revisions.json'

    @classmethod
    def tearDownClass(cls): cls.directory.cleanup()

    def setUp(self):
        self.package.write_text(json.dumps(self.original))
        self.oracle_path.write_text(json.dumps(self.oracle))

    def check_case(self,revision=None,success=True,message=None):
        self.revisions_path.write_text(json.dumps({'formatVersion':1,'revisions':{} if revision is None else {'cube3':revision}}))
        result=subprocess.run([str(EXECUTABLE),'--check',str(self.oracle_path),str(self.revisions_path)],
                              cwd=self.workspace,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        self.assertEqual(result.returncode==0,success,result.stdout+result.stderr)
        if message:self.assertIn(message,result.stderr)
        return json.loads(result.stdout) if success else None

    def test_unchanged_rules_match_original_oracle(self):
        self.assertIsNone(self.check_case()['ruleRevision'])

    def test_unregistered_goal_change_fails(self):
        self.package.write_text(json.dumps(self.revised))
        self.check_case(success=False,message='without a declared rule revision')

    def test_explicit_revision_checks_source_and_compiled_pin(self):
        self.package.write_text(json.dumps(self.revised))
        self.assertEqual(self.check_case(self.revision)['ruleRevision'],'1'*40)

    def test_revision_cannot_mask_changed_immutable_signature(self):
        self.package.write_text(json.dumps(self.revised))
        oracle=json.loads(json.dumps(self.oracle));oracle['semantics']['goal']='0'*64
        self.oracle_path.write_text(json.dumps(oracle))
        self.check_case(self.revision,success=False,message='Frozen semantics changed')

    def test_revision_cannot_mask_wrong_legacy_pin(self):
        self.package.write_text(json.dumps(self.revised))
        self.check_case({**self.revision,'legacyDefinitionDigest':'0'*64},success=False,message='wrong legacy definition pin')

    def test_revision_cannot_mask_subsequent_source_change(self):
        changed=json.loads(json.dumps(self.revised));changed['source']['goal']=True
        self.package.write_text(json.dumps(changed))
        self.check_case(self.revision,success=False,message='source/catalog differs')

    def test_revision_cannot_mask_compiled_rule_change(self):
        self.package.write_text(json.dumps(self.revised))
        self.check_case({**self.revision,'definitionDigest':'0'*64},success=False,message='compiled definition differs')

    def test_revision_cannot_bypass_transport_comparison(self):
        changed=json.loads(json.dumps(self.revised))
        moves=changed['source']['domains'][0]['moves']
        moves[0]['map']=next(move['map'] for move in moves if move['map']!=moves[0]['map'])
        self.package.write_text(json.dumps(changed))
        self.check_case(self.revision,success=False,message='Changed moveMaps')


if __name__=='__main__': unittest.main(verbosity=2)
