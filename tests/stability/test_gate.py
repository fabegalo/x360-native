"""Adversarial certification fixtures, no original content or UI."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('gate', Path(__file__).resolve().parents[2] / 'scripts/validation/stability_gate.py')
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class Certification(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        subprocess.run(['git', 'init', '-q', str(root)], check=True)
        (root / 'deps.lock.json').write_text('{}')
        subprocess.run(['git', '-C', str(root), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(root), '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                        'commit', '-qm', 'test: fixture'], check=True)
        self.policy = {'tests': ['contract'], 'manual': ['real-save'], 'artifact_roles': ['runtime']}
        self.file = root / 'runtime.exe'
        self.file.write_bytes(b'synthetic binary')
        self.save = root / 'progress'
        self.save.write_bytes(b'level=17')
        identity = {'commit': gate.git(root, 'rev-parse', 'HEAD'), 'sdk_commit': 'a' * 40,
                    'lock_sha256': gate.digest(root / 'deps.lock.json'), 'policy_sha256': gate.digest(gate.POLICY)}
        self.report = {'schema': 1, 'configuration': 'Windows-x64/Release/Xenos/D3D12/17559',
                       'source': str(root), 'identity': identity, 'preflight': 'PASS',
                       'artifact_directories': [str(root)],
                       'artifacts': [dict(gate.artifact(self.file), role='runtime')],
                       'preflight_evidence': [gate.artifact(self.file)], 'results': [], 'junit': {}}
        for label, names in [('contracts', ['contract']), ('desktop', ['desktop.catalog', 'desktop.update-recovery']),
                             ('synthetic', ['stable session crash and relaunch preserve the single HDD bytes'])]:
            xml = root / (label + '.xml')
            xml.write_text('<testsuites><testsuite>' + ''.join(f'<testcase name="{name}"/>' for name in names) + '</testsuite></testsuites>')
            self.report['junit'][label] = gate.artifact(xml)
            for name in names:
                self.report['results'].append({'id': 'synthetic-recovery' if label == 'synthetic' else name,
                    'state': 'PASS', 'evidence': [gate.artifact(xml)]})
        self.report['results'].append({'id': 'real-save', 'state': 'PASS', 'identity': copy.deepcopy(identity),
            'content': [gate.artifact(self.save)], 'observation': 'Synthetic verifier fixture only',
            'evidence': [gate.artifact(self.save)]})

    def errors(self):
        return gate.verify(self.report, self.policy)

    def test_complete_synthetic_evidence_accepted(self):
        self.assertEqual(self.errors(), [])

    def test_all_nonpass_states_block_even_historical_failures(self):
        for state in ['FAIL', 'KNOWN FAILURE', 'SKIP', 'NOT RUN', 'unknown']:
            self.report['results'][0]['state'] = state
            self.assertTrue(self.errors(), state)

    def test_missing_result_blocks(self):
        self.report['results'].pop(0)
        self.assertTrue(self.errors())

    def test_duplicate_result_blocks(self):
        self.report['results'].append(self.report['results'][0])
        self.assertTrue(self.errors())

    def test_missing_evidence_blocks(self):
        self.report['results'][0]['evidence'] = []
        self.assertTrue(self.errors())

    def test_omitted_artifact_blocks(self):
        self.report['artifacts'] = []
        self.assertTrue(self.errors())

    def test_omitted_sibling_dependency_blocks(self):
        (self.file.parent / 'dependency.dll').write_bytes(b'not in manifest')
        self.assertTrue(self.errors())

    def test_changed_binary_blocks(self):
        self.file.write_bytes(b'changed binary')
        self.assertTrue(self.errors())

    def test_corrupt_or_truncated_save_blocks(self):
        self.save.write_bytes(b'level=')
        self.assertTrue(self.errors())

    def test_content_identity_change_blocks_even_with_new_evidence_hash(self):
        self.report['results'][-1]['identity']['sdk_commit'] = 'b' * 40
        self.assertTrue(self.errors())

    def test_lock_identity_change_blocks(self):
        (self.file.parent / 'deps.lock.json').write_text('{"changed":true}')
        self.assertTrue(self.errors())

    def test_wrong_backend_blocks(self):
        self.report['configuration'] = 'Windows-x64/Release/Native/Vulkan/17559'
        self.assertTrue(self.errors())

    def test_failed_build_cannot_use_stale_binaries(self):
        self.report['preflight'] = 'FAIL'
        self.assertTrue(self.errors())

    def test_xml_failure_cannot_be_changed_to_pass(self):
        xml = Path(self.report['junit']['contracts']['path'])
        xml.write_text('<testsuite><testcase name="contract"><failure/></testcase></testsuite>')
        self.report['junit']['contracts'] = gate.artifact(xml)
        self.assertTrue(self.errors())

    def test_empty_or_duplicate_junit_rejected(self):
        xml = Path(self.report['junit']['contracts']['path'])
        for text in ['<testsuite/>', '<testsuite><testcase name="contract"/><testcase name="contract"/></testsuite>']:
            xml.write_text(text)
            with self.assertRaises(ValueError):
                gate.junit(xml, ['contract'])

    def test_missing_original_attestation_blocks(self):
        self.report['results'][-1].pop('observation')
        self.assertTrue(self.errors())

    def test_reuse_preserves_failed_result(self):
        xml = Path(self.report['junit']['contracts']['path'])
        xml.write_text('<testsuite><testcase name="contract"><failure/></testcase></testsuite>')
        self.report['junit']['contracts'] = gate.artifact(xml)
        self.report['results'][0].update(state='FAIL', evidence=[gate.artifact(xml)])
        rows, _ = gate.reuse_contracts(self.report, copy.deepcopy(self.report), self.policy, self.file.parent)
        self.assertEqual(rows, [{'id': 'contract', 'state': 'FAIL'}])

    def test_reuse_refuses_changed_identity_or_omitted_artifact(self):
        for key in ['identity', 'artifacts']:
            previous = copy.deepcopy(self.report)
            if key == 'identity':
                previous['identity']['commit'] = 'b' * 40
            else:
                previous['artifacts'] = []
            with self.assertRaises(ValueError):
                gate.reuse_contracts(previous, self.report, self.policy, self.file.parent)

    def test_command_timeout_stops_the_child(self):
        log = self.file.parent / 'timeout.log'
        rc = gate.command([sys.executable, '-c', 'import time; time.sleep(30)'], self.file.parent, log, 1)
        self.assertEqual(rc, 124)
        self.assertIn('exceeded bounded timeout', log.read_text())


if __name__ == '__main__':
    unittest.main()
