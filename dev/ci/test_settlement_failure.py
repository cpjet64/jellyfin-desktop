"""Offline protocol tests using injected operations and an in-memory channel."""
import json
import unittest

import settlement_failure as protocol


def cleanup_success(_handle):
    return {'requested': True, 'operation': 'success', 'leader_reap': 'success',
            'group_settlement': 'success'}


def valid_operation(observation):
    observation.identity('own_leader', 'success', {'pid': 20})
    observation.command('inventory', {'exit': 0})
    return 'ok'


def run_capture(operation, cleanup=cleanup_success, spawn=None):
    output = []
    if spawn is None:
        spawn = lambda register: register('owned')
    try:
        result = protocol.run(spawn, operation, cleanup, output.append, clock=lambda: 1)
    except protocol.ProtocolFailure as error:
        return json.loads(output[0]), error
    return json.loads(output[0]), result


class SettlementFailureTests(unittest.TestCase):
    def test_nested_secondary_details_survive_run(self):
        def fail(_observation):
            raise protocol.Failure('inventory', 'timeout',
                                   ['fd_close_failed', 'mask_restore_failed', 'private detail'])
        nested = protocol.nested(fail, protocol.Observation())
        self.assertEqual(nested['secondary'], ['fd_close_failed', 'mask_restore_failed'])
        self.assertNotIn('private detail', json.dumps(nested))
        record, error = run_capture(fail)
        self.assertEqual(record['failure'], {'phase': 'inventory', 'category': 'timeout'})
        self.assertEqual(record['secondary'], [
            {'phase': 'cleanup', 'category': 'unknown', 'group': None, 'detail': 'fd_close_failed'},
            {'phase': 'cleanup', 'category': 'unknown', 'group': None, 'detail': 'mask_restore_failed'}])
        self.assertEqual(record['cleanup'][0]['group_settlement'], 'success')
        self.assertEqual(error.record, record)
        self.assertNotIn('private detail', json.dumps(record))

    def test_nested_failure_categories_survive_parent(self):
        for phase, category in (('inventory', 'timeout'), ('inventory', 'output_bound'),
                                ('inventory', 'nonzero'), ('held_identity', 'identity_unavailable'),
                                ('identification', 'identity_mismatch'), ('inventory', 'malformed_inventory')):
            def fail(_observation):
                raise protocol.Failure(phase, category)
            with self.subTest(category=category):
                record, error = run_capture(fail)
                self.assertEqual(record['failure'], {'phase': phase, 'category': category})
                self.assertEqual(error.record, record)
                self.assertEqual(record['cleanup'][0]['group_settlement'], 'success')

    def test_command_nonzero_and_output_bound_are_failures(self):
        for values, expected in (({'exit': 7, 'output_bytes': 12}, 'nonzero'),
                                 ({'exit': 0, 'output_bound': True}, 'output_bound'),
                                 ({'exit': 0, 'truncated': True}, 'output_bound')):
            with self.subTest(values=values):
                record, _ = run_capture(lambda obs: obs.command('inventory', values))
                self.assertEqual(record['failure']['category'], expected)
                self.assertEqual(record['events'][0]['values'], values)

    def test_first_failure_survives_cleanup_failure_and_unknown(self):
        def fail(_observation):
            raise protocol.Failure('identification', 'malformed_inventory')
        for cleanup in (lambda _: {'requested': True, 'operation': 'failed', 'leader_reap': 'success',
                                   'group_settlement': 'unknown'},
                        lambda _: (_ for _ in ()).throw(OSError('private detail'))):
            record, _ = run_capture(fail, cleanup)
            self.assertEqual(record['failure']['category'], 'malformed_inventory')
            self.assertEqual(record['secondary'][0]['phase'], 'cleanup')
            self.assertNotIn('private detail', json.dumps(record))

    def test_success_with_unknown_or_unrequested_cleanup_fails(self):
        for cleanup in (lambda _: {'requested': True, 'operation': 'success', 'leader_reap': 'success',
                                  'group_settlement': 'unknown'},
                        lambda _: {'requested': False, 'operation': 'success', 'leader_reap': 'success',
                                   'group_settlement': 'success'}):
            record, error = run_capture(valid_operation, cleanup)
            self.assertIsInstance(error, protocol.ProtocolFailure)
            self.assertEqual(record['failure']['phase'], 'cleanup')

    def test_pre_spawn_and_partial_spawn(self):
        def no_spawn(_register):
            raise protocol.Failure('launch', 'unknown')
        record, _ = run_capture(lambda _: None, spawn=no_spawn)
        self.assertEqual(record['cleanup'], [])
        self.assertEqual(record['failure']['phase'], 'launch')
        def partial(register):
            register('owned')
            raise protocol.Failure('launch', 'timeout')
        record, _ = run_capture(lambda _: None, spawn=partial)
        self.assertEqual(record['failure']['category'], 'timeout')
        self.assertEqual(record['cleanup'][0]['leader_reap'], 'success')

    def test_identity_error_and_distinct_states_are_retained(self):
        def operation(observation):
            observation.identity('own_leader', 'unknown',
                                 {'api_result': -1, 'api_error': 3, 'expected_size': 136, 'returned_size': 0})
            observation.identity('observed_row', 'disappeared', {'pid': 42, 'uid': 501})
            raise protocol.Failure('held_identity', 'identity_unavailable')
        record, _ = run_capture(operation)
        self.assertEqual([event['state'] for event in record['events']], ['unknown', 'disappeared'])
        self.assertEqual(record['events'][0]['values']['expected_size'], 136)

    def test_success_and_channel_failure(self):
        record, result = run_capture(valid_operation)
        self.assertTrue(record['ok'])
        self.assertEqual(result, 'ok')
        with self.assertRaises(protocol.ProtocolFailure) as raised:
            protocol.run(lambda register: register('owned'), valid_operation, cleanup_success,
                         lambda _: (_ for _ in ()).throw(OSError('channel unavailable')))
        self.assertFalse(raised.exception.record['ok'])

    def test_missing_command_and_uncertain_identity_cannot_pass(self):
        record, _ = run_capture(lambda obs: obs.identity('own_leader', 'success', {'pid': 1}))
        self.assertEqual(record['failure']['category'], 'unknown')
        def uncertain(obs):
            obs.identity('own_leader', 'unknown', {'pid': 1})
            obs.command('inventory', {'exit': 0})
        record, _ = run_capture(uncertain)
        self.assertEqual(record['failure']['category'], 'identity_unavailable')

    def test_event_bound_never_passes_and_preserves_primary(self):
        def operation(observation):
            for _ in range(protocol.MAX_EVENTS + 1):
                observation.identity('observed_row', 'unknown', {'pid': 1})
        record, _ = run_capture(operation)
        self.assertEqual(record['failure']['category'], 'output_bound')
        self.assertEqual(len(record['events']), protocol.MAX_EVENTS)
        self.assertLessEqual(len(json.dumps(record).encode()), protocol.MAX_RECORD)

    def test_serialization_bound_keeps_cleanup_and_first_failure(self):
        original = {'ok': False, 'failure': {'phase': 'inventory', 'category': 'timeout'},
                    'secondary': [], 'events': ['x' * protocol.MAX_RECORD],
                    'cleanup': [cleanup_success('owned')], 'record_truncated': False}
        encoded = protocol.encode(original)
        result = json.loads(encoded)
        self.assertLessEqual(len(encoded), protocol.MAX_RECORD)
        self.assertTrue(result['record_truncated'])
        self.assertEqual(result['failure'], original['failure'])
        self.assertEqual(result['cleanup'], original['cleanup'])


if __name__ == '__main__':
    unittest.main()
