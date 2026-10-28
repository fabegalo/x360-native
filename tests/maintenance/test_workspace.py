import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('audit', Path(__file__).resolve().parents[2] / 'scripts/maintenance/workspace_audit.py')
a = importlib.util.module_from_spec(spec); spec.loader.exec_module(a)


class Maintenance(unittest.TestCase):
    def setUp(self):
        scratch = Path(os.environ.get('X360N_TEST_TEMP', str(Path.cwd()/'.local/tmp')))
        scratch.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=scratch)
        self.base = Path(self.tmp.name)
        self.root = self.base / 'workspace'; self.root.mkdir()
        self.out = self.base / 'report'
        self.obj = self.put('.local/build/test/CMakeFiles/a.dir/same.obj', b'a'*32)
        self.put('CMakeLists.txt',b'# synthetic source fixture')
        self.put('.local/build/test/CMakeCache.txt',('# This is the CMakeCache file.\nCMAKE_HOME_DIRECTORY:INTERNAL='+str(self.root)+'\n').encode())
        self.put('.local/build/test/build.ninja',b'# CMAKE generated file: DO NOT EDIT!\nbuild CMakeFiles/a.dir/same.obj: CXX_COMPILER source.cpp\n')
        self.put('.local/environment.lock', b'')
        self.patches = [patch.object(a, 'worktree_paths', return_value=[]), patch.object(a, 'processes', return_value=[])]
        for p in self.patches: p.start()

    def tearDown(self):
        for p in reversed(self.patches): p.stop()
        self.tmp.cleanup()

    def put(self, rel, data):
        p = self.root / rel; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(data); return p

    def audit(self):
        a.audit(self.root, self.out, 1, 60)
        return json.loads((self.out/'plan.json').read_text())

    def test_dry_run_and_protected(self):
        for folder in ['inputs','user','keys','catalog','toolchains','worktrees','evidence/stable-02/persistence-01/working']:
            self.put('.local/'+folder+'/CMakeFiles/same.obj', b'a'*32)
        before = {str(p):a.digest(p) for p,s,k in a.walk(self.root) if k=='file'}
        plan = self.audit()
        after = {str(p):a.digest(p) for p,s,k in a.walk(self.root) if k=='file'}
        self.assertEqual(before, after); self.assertEqual(len(plan['items']), 1)

    def test_same_names_different_content_and_final_binaries(self):
        self.put('.local/evidence/old/same.obj', b'b'*32)
        for name in a.TOOLS:
            self.put('.local/build/test/src/desktop/'+name, b'fixture')
        self.audit(); a.execute(self.root,self.out)
        self.assertTrue((self.root/'.local/evidence/old/same.obj').exists())
        for name in a.TOOLS: self.assertTrue((self.root/'.local/build/test/src/desktop'/name).exists())

    def test_hardlink_is_counted_once_and_never_deleted(self):
        os.link(self.obj, self.obj.with_name('hard.obj'))
        plan = self.audit(); self.assertEqual(plan['items'], [])
        with a.sqlite3.connect(self.out/'inventory.sqlite') as db:
            ids = list(db.execute("SELECT identity FROM files WHERE path LIKE '%.obj'"))
        db.close()
        self.assertEqual(ids[0], ids[1])

    def test_junction_is_not_followed(self):
        outside = self.base/'outside'; outside.mkdir(); (outside/'secret.obj').write_bytes(b'x')
        link = self.obj.parent/'redirect'
        result = subprocess.run(['cmd','/c','mklink','/J',str(link),str(outside)], capture_output=True)
        self.assertEqual(result.returncode,0)
        try:
            self.audit()
            with self.assertRaises(ValueError): a.safe_path(self.root,str(link/'secret.obj'))
            with a.sqlite3.connect(self.out/'inventory.sqlite') as db:
                self.assertEqual(db.execute("SELECT COUNT(*) FROM files WHERE path LIKE '%secret%'").fetchone()[0],0)
            db.close()
        finally:
            os.rmdir(link)

    def test_unknown_receipt_blocks_related_build_cleanup(self):
        self.put('.local/build/test/a.exe.preparation.json', b'{broken')
        self.assertEqual(self.audit()['items'], [])
        a.execute(self.root,self.out)
        self.assertTrue(self.obj.exists())

    def test_receipt_reference_and_new_receipt(self):
        plan = self.audit(); self.assertEqual(len(plan['items']),1)
        receipt = {'schema':1,'inputs':[{'path':str(self.obj)}], 'artifacts':[{'path':'other'}]}
        self.put('.local/build/other/a.exe.preparation.json',json.dumps(receipt).encode())
        with self.assertRaises(ValueError): a.execute(self.root,self.out)
        self.assertTrue(self.obj.exists())

    def test_reference_report(self):
        self.put('docs/report.md',str(self.obj).encode())
        self.assertEqual(self.audit()['items'],[])

    def test_active_process_blocks(self):
        self.audit()
        with patch.object(a,'processes',return_value=[{'pid':123,'name':'x360-native.exe'}]):
            with self.assertRaises(RuntimeError): a.execute(self.root,self.out)
        self.assertTrue(self.obj.exists())

    def test_changed_content_blocks(self):
        self.audit(); self.obj.write_bytes(b'z'*32)
        with self.assertRaises(ValueError): a.execute(self.root,self.out)
        self.assertTrue(self.obj.exists())

    def test_resume_never_deletes_recreated_path(self):
        self.audit(); a.execute(self.root,self.out)
        self.assertFalse(self.obj.exists())
        a.execute(self.root,self.out)
        self.obj.write_bytes(b'recreated')
        with self.assertRaises(ValueError): a.execute(self.root,self.out)
        self.assertEqual(self.obj.read_bytes(),b'recreated')

    def test_open_file_blocks_exclusive_deletion(self):
        self.audit()
        with self.obj.open('rb'):
            with self.assertRaises(OSError): a.execute(self.root,self.out)
        self.assertTrue(self.obj.exists())

    def test_outside_and_forged_protected_item(self):
        with self.assertRaises(ValueError): a.safe_path(self.root,'../outside')
        plan = self.audit(); plan['items'][0]['path'] = '.local/user/CMakeFiles/same.obj'
        self.put(plan['items'][0]['path'],b'a'*32)
        a.write_json(self.out/'plan.json',plan)
        with self.assertRaises(ValueError): a.execute(self.root,self.out)

    def test_unproven_object_is_preserved(self):
        self.put('.local/build/unknown/CMakeFiles/unique.obj',b'unproven research')
        self.assertEqual(len(self.audit()['items']),1)

    def test_selected_user_storage_under_build_is_protected(self):
        self.put('.local/user-data-root.txt',str(self.obj.parent).encode())
        self.assertEqual(self.audit()['items'],[])


if __name__ == '__main__': unittest.main()
