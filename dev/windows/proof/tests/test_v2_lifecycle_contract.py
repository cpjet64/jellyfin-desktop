"""Focused v2 settlement source cases; requires the separately assembled package."""
import unittest
from contextlib import ExitStack

from build_only_v2 import lifecycle


class Process:
    def __init__(self, pid):
        self.pid = pid

    def __enter__(self):
        return self

    def __exit__(self, *_):
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
