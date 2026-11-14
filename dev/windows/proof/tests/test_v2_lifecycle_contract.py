"""Focused v2 settlement source cases; requires the separately assembled package."""
import unittest
import sys
from contextlib import ExitStack
from types import SimpleNamespace
from unittest.mock import patch

from build_only_v2 import lifecycle
from build_only_v2 import supervisor


class Process:
    def __init__(self, pid):
        self.pid = pid

    def __enter__(self):
        return self

    def __exit__(self, *_):
        pass

    def close(self):
        pass


class API:
    def __init__(self, images):
        self.images = images
        self.terminated = []

    def members(self, _job):
        return tuple(self.images)

    def open_process(self, pid):
        return Process(pid)

    def image_path(self, process):
        return self.images[process.pid]

    def in_job(self, _process, _job):
        return True


class Pin:
    def __init__(self, path):
        self.path = path
        self.final_path = '\\\\?\\' + path

    def verify_process(self, _api, process, _job):
        return {'creation_time': process.pid, 'path': self.path}


class IdentifyTests(unittest.TestCase):
    def setUp(self):
        self.pins = {'vctip': Pin(r'C:\tools\vctip.exe'),
                     'mspdbsrv': Pin(r'C:\tools\mspdbsrv.exe')}

    def identify(self, images, pins=None):
        api = API(images)
        evidence = {'residuals': []}
        with ExitStack() as stack:
            lifecycle._identify(api, None, pins or self.pins, {'active': len(images)},
                                10, lambda: 0, stack, evidence, lifecycle.CleanupHandoff())
        return evidence['residuals']

    def test_mixed_roles_are_all_identified(self):
        items = self.identify({1: self.pins['vctip'].path, 2: self.pins['mspdbsrv'].path})
        self.assertEqual([item['role'] for item in items], ['vctip', 'mspdbsrv'])
        self.assertTrue(all(item['action'] == 'none' for item in items))

    def test_unknown_and_duplicate_roles_fail(self):
        for images in ({1: r'C:\tools\other.exe'},
                       {1: self.pins['vctip'].path, 2: self.pins['vctip'].path}):
            with self.subTest(images=images), self.assertRaises(lifecycle.SettlementError):
                self.identify(images)

    def test_ambiguous_path_fails(self):
        pins = {'vctip': Pin(r'C:\tools\same.exe'), 'mspdbsrv': Pin(r'C:\tools\same.exe')}
        with self.assertRaises(lifecycle.SettlementError):
            self.identify({1: r'C:\tools\same.exe'}, pins)

    def test_deadline_fails_before_intervention(self):
        with self.assertRaises(lifecycle.SettlementError):
            lifecycle._milliseconds(1, lambda: 1)

    def test_late_association_prevents_intervention(self):
        api = API({1: self.pins['vctip'].path})
        api.accounting = lambda _job: (1, 3)
        evidence = {'baseline': {'total': 2}, 'residuals': [{'identity': {'creation_time': 1}}]}
        with self.assertRaises(lifecycle.SettlementError):
            lifecycle._intervene(api, None, [(1, Process(1))], evidence, 10, lambda: 0)
        self.assertEqual(api.terminated, [])

    def test_failed_termination_is_not_settlement(self):
        api = API({1: self.pins['vctip'].path})
        api.accounting = lambda _job: (1, 2)
        api.wait = lambda _process, _timeout: False
        api.creation_time = lambda process: process.pid
        def fail(_process, _code):
            raise OSError('termination failed')
        api.terminate = fail
        evidence = {'baseline': {'total': 2}, 'residuals': [{'identity': {'creation_time': 1}}]}
        with self.assertRaises(OSError):
            lifecycle._intervene(api, None, [(1, Process(1))], evidence, 10, lambda: 0)

    def test_root_failure_is_rejected(self):
        api = API({})
        api.creation_time = lambda _process: 1
        root = type('Root', (), {'process': Process(1), 'wait': lambda self, _: True,
                                 'exit_code': lambda self: 1})()
        with self.assertRaises(lifecycle.SettlementError):
            lifecycle._root_exit(api, root, 10, lambda: 0, {'root': {}})

    def test_failed_close_retains_exact_process(self):
        api = API({1: self.pins['vctip'].path})
        process = Process(1)
        def fail():
            raise OSError('close failed')
        process.close = fail
        api.open_process = lambda _pid: process
        handoff = lifecycle.CleanupHandoff()
        with self.assertRaises(OSError):
            with ExitStack() as stack:
                lifecycle._identify(api, None, self.pins, {'active': 1}, 10,
                                    lambda: 0, stack, {'residuals': []}, handoff)
        self.assertIs(handoff.handles[0], process)

    def test_settle_root_failure_requires_emergency(self):
        api = API({})
        api.creation_time = lambda _process: 1
        api.terminate_job = lambda _job, _code: None
        api.accounting = lambda _job: (0, 1)
        root = type('Root', (), {'pid': 1, 'process': Process(1), 'wait': lambda self, _: True,
                                 'exit_code': lambda self: 1, 'close': lambda self: None})()
        outcome = lifecycle.settle(api, None, root, self.pins, 10,
                                   handoff=lifecycle.CleanupHandoff(), clock=lambda: 0)
        self.assertEqual(outcome['result'], 'failed')
        self.assertTrue(outcome['emergency'])
        self.assertEqual(outcome['cleanup_deadline'], 10)

    def test_unresolved_root_remains_in_handoff(self):
        api = API({})
        api.creation_time = lambda _process: 1
        api.terminate_job = lambda _job, _code: None
        api.accounting = lambda _job: (0, 1)
        root = type('Root', (), {'pid': 1, 'process': Process(1), 'wait': lambda self, _: False})()
        handoff = lifecycle.CleanupHandoff()
        outcome = lifecycle.settle(api, None, root, self.pins, 10, handoff=handoff, clock=lambda: 0)
        self.assertTrue(outcome['root']['unresolved'])
        self.assertIs(handoff.root, root)

    def test_emergency_reuses_cleanup_deadline_after_failure(self):
        api = API({})
        root = type('Root', (), {'pid': 1})()
        handoff = lifecycle.CleanupHandoff()
        def root_exit(_api, _root, _deadline, _clock, evidence):
            evidence['root'].update(observed_at=1, closed=True)
        def cleanup(*_args):
            raise OSError('post-intervention count changed')
        with patch.object(lifecycle, '_root_exit', root_exit), patch.object(lifecycle, '_grace'), \
                patch.object(lifecycle, '_cleanup', cleanup), patch.object(lifecycle, '_emergency') as emergency:
            outcome = lifecycle.settle(api, None, root, self.pins, 20, handoff=handoff, clock=lambda: 2)
        self.assertEqual(outcome['result'], 'failed')
        self.assertEqual(outcome['cleanup_deadline'], 12)
        self.assertEqual(emergency.call_args.args[3], 12)

    def test_final_count_change_after_intervention_fails(self):
        api = API({1: self.pins['vctip'].path})
        counts = iter(((1, 2), (1, 2), (1, 2), (0, 3)))
        api.accounting = lambda _job: next(counts)
        api.creation_time = lambda process: process.pid
        api.wait = lambda _process, timeout: timeout != 0
        api.terminate = lambda process, _code: api.terminated.append(process.pid)
        evidence = {'residuals': []}
        with self.assertRaises(lifecycle.SettlementError):
            lifecycle._cleanup(api, None, self.pins, 10, lambda: 0,
                               evidence, lifecycle.CleanupHandoff())
        self.assertEqual(api.terminated, [1])
        self.assertEqual(evidence['final']['total'], 3)


class SupervisorFailureTests(unittest.TestCase):
    def run_case(self, *, assembly=False, write=False, close=False, output=False):
        class Receipt:
            def __enter__(self):
                return self
            def __exit__(self, *_):
                if close:
                    raise OSError('close failed')
            def write(self, data):
                if write:
                    raise OSError('write failed')
                self.data = data
            def flush(self):
                pass
        receipt = Receipt()
        api = SimpleNamespace(opened=1, closed=1, close_failed=0, in_any_job=lambda: True)
        old = SimpleNamespace(_receipt=lambda _: receipt, _close_retained=lambda _: [])
        winapi = SimpleNamespace(WinAPI=lambda: api)
        profile = SimpleNamespace(validate_profile=lambda _: None, validate_environment=lambda *_: None)
        def job(_api, _profile, _env, _handoff, state, started, _old):
            started.append(1)
            state['outcome'] = {'result': 'natural'}
            if output:
                raise OSError('output observation failed')
        def assemble(*_args):
            if assembly:
                raise ValueError('assembly failed')
            return {'schema': 'build-only-v2'}
        with patch.dict(sys.modules, {'build_only_v2.profile': profile}), \
                patch.object(supervisor, '_checked_runtime', return_value=(old, winapi)), \
                patch.object(supervisor, '_run_job', job), patch.object(supervisor, 'assemble', assemble):
            with self.assertRaises(Exception) as caught:
                supervisor._run_owned(None, {}, 'fixed')
        self.assertIsInstance(caught.exception.cleanup_handoff, lifecycle.CleanupHandoff)
        return receipt

    def test_assembly_failure_writes_failure_receipt(self):
        receipt = self.run_case(assembly=True)
        self.assertIn(b'build-only-failed-v2', receipt.data)
        self.assertIn(b'assembly failed', receipt.data)

    def test_output_failure_writes_failure_receipt(self):
        receipt = self.run_case(output=True)
        self.assertIn(b'output observation failed', receipt.data)

    def test_receipt_write_failure_keeps_handoff(self):
        self.run_case(write=True)

    def test_receipt_close_failure_keeps_handoff(self):
        self.run_case(close=True)
