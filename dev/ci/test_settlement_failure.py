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
    def test_role_session_is_live_for_held_and_historical_for_command(self):
        from unittest.mock import Mock
        expected = {'pid': 43, 'ppid': 42, 'uid': 501, 'pgid': 42, 'session': 42,
                    'start_sec': 1, 'start_usec': 2}
        row = dict(expected, session=None, ps_session_metadata='abcdef')
        kernel = {key: expected[key] for key in ('pid', 'ppid', 'uid', 'pgid', 'start_sec', 'start_usec')}
        identity, sid = Mock(return_value=kernel), Mock(return_value=42)
        observation = protocol.Observation()
        protocol.compare_owned_row('leader', expected, row, 1, observation, identity, sid)
        self.assertEqual(identity.call_count, 2)
        sid.assert_called_once_with(43)
        event = observation.events[-1]
        self.assertEqual([field['state'] for field in event['fields']], ['match'] * 6)
        self.assertEqual(event['ps_session_metadata'], 'abcdef')
        identity.reset_mock(); sid.reset_mock()
        protocol.compare_owned_row('command', expected, row, 1, observation, identity, sid)
        identity.assert_not_called(); sid.assert_not_called()
        self.assertEqual(observation.events[-1]['fields'][4]['state'], 'not_applicable')
        self.assertEqual(observation.events[-1]['fields'][4]['expected_source'], 'getsid(pre_exec)')
        self.assertEqual(observation.events[-1]['fields'][4]['expected'], 42)
        self.assertEqual(observation.events[-1]['fields'][4]['observed_source'], 'not_applicable_completed')
        self.assertEqual(observation.events[-1]['scope'], 'historical_completed_not_current_ownership')

    def test_held_identity_and_session_fail_closed_in_order(self):
        from unittest.mock import Mock
        expected = {'pid': 43, 'ppid': 42, 'uid': 501, 'pgid': 42, 'session': 42,
                    'start_sec': 1, 'start_usec': 2}
        kernel = {key: expected[key] for key in ('pid', 'ppid', 'uid', 'pgid', 'start_sec', 'start_usec')}
        for values, sid_effect, expected_category, sid_count in (
            ([{}, kernel], 42, 'identity_unavailable', 0),
            ([kernel, {}], 42, 'identity_unavailable', 1),
            ([kernel, kernel], ProcessLookupError('private'), 'identity_unavailable', 1),
            ([kernel, kernel], PermissionError('private'), 'identity_unavailable', 1),
            ([kernel, kernel], OSError('private'), 'identity_unavailable', 1),
            ([kernel, kernel], 99, 'identity_mismatch', 1)):
            identity, sid = Mock(side_effect=values), Mock(side_effect=sid_effect) if isinstance(sid_effect, Exception) else Mock(return_value=sid_effect)
            observation = protocol.Observation()
            with self.assertRaises(protocol.Failure) as error:
                protocol.compare_owned_row('probe', expected, expected, 1, observation, identity, sid)
            self.assertEqual(error.exception.category, expected_category)
            self.assertEqual(sid.call_count, sid_count)
            self.assertEqual(observation.events[-1]['fields'][5]['state'], 'not_evaluated')
        identity, sid = Mock(), Mock()
        with self.assertRaises(protocol.Failure):
            protocol.compare_owned_row('leader', expected, dict(expected, ppid=99), 1,
                                       protocol.Observation(), identity, sid)
        identity.assert_not_called(); sid.assert_not_called()
        with self.assertRaises(protocol.Failure) as error:
            protocol.compare_owned_row('leader', expected, expected, 1, protocol.Observation(), Mock(return_value=kernel), None)
        self.assertEqual(error.exception.category, 'identity_unavailable')

    def test_identity_comparison_retains_short_circuit_and_values(self):
        names = protocol.IDENTITY_COMPARISON_FIELDS
        expected = {name: index + 1 for index, name in enumerate(names)}
        self.assertTrue(all(item['state'] == 'match'
                            for item in protocol.identity_comparison(expected, expected)['fields']))
        for index, name in enumerate(names):
            observed = dict(expected, **{name: 100})
            fields = protocol.identity_comparison(expected, observed)['fields']
            self.assertEqual([item['state'] for item in fields],
                             ['match'] * index + ['mismatch'] + ['not_evaluated'] * (5 - index))
            self.assertEqual(fields[index]['expected'], expected[name])
            self.assertEqual(fields[index]['observed'], 100)
        observed = dict(expected)
        del observed['session']
        fields = protocol.identity_comparison(expected, observed)['fields']
        self.assertEqual(fields[4]['state'], 'unknown')
        self.assertEqual(fields[5]['state'], 'not_evaluated')

    def test_own_row_parse_failures_are_distinct_and_bounded(self):
        identity = {'pid': 43, 'ppid': 42, 'uid': 501, 'pgid': 42, 'session': 42, 'start_sec': 1}
        base = ['43', '42', '42', '42', '501', 'Mon', 'Jan', '01', '12:00:00', '2024', 'command']
        for index, field in ((1, 'ppid'), (2, 'pgid'), (5, 'start_sec')):
            parts = list(base)
            parts[index] = 'unparseable'
            observation = protocol.Observation()
            with self.subTest(field=field), self.assertRaises(protocol.Failure):
                protocol.parse_inventory_row(' '.join(parts), (identity,) * 3, observation)
            fields = observation.events[0]['fields']
            self.assertEqual(next(item for item in fields if item['field'] == field)['state'], 'unknown')
            self.assertTrue(all(item['state'] in ('unknown', 'not_evaluated') for item in fields))
        observation = protocol.Observation()
        with self.assertRaises(protocol.Failure):
            protocol.parse_inventory_row('unparseable ' + ' '.join(base[1:]), (identity,) * 3, observation)
        self.assertEqual(observation.events, [])

    def test_parse_failure_ownership_and_row_shape(self):
        identity = {'pid': 43, 'ppid': 42, 'uid': 501, 'pgid': 42, 'session': 42, 'start_sec': 1}
        for line in ('44 42 bad 42 501 Mon Jan 01 12:00:00 2024 command',
                     '43 42 bad 42 502 Mon Jan 01 12:00:00 2024 command',
                     '43 42 bad 42 secret Mon Jan 01 12:00:00 2024 command', '43 42 42 42'):
            observation = protocol.Observation()
            with self.subTest(line=line), self.assertRaises(protocol.Failure):
                protocol.parse_inventory_row(line, (identity,) * 3, observation)
            self.assertEqual(observation.events, [])
        for line in ('43 42 42 42 501 Mon Jan 01 12:00:00 2024', '43 42 42 42 501 Mon Jan'):
            observation = protocol.Observation()
            with self.subTest(line=line), self.assertRaises(protocol.Failure):
                protocol.parse_inventory_row(line, (identity,) * 3, observation)
            event = observation.events[0]
            self.assertEqual(event['reason'], 'row_shape')
            self.assertTrue(all(item['state'] == 'not_evaluated' for item in event['fields']))

    def test_parse_failure_never_records_untrusted_tokens(self):
        identity = {'pid': 43, 'ppid': 42, 'uid': 501, 'pgid': 42, 'session': 42, 'start_sec': 1}
        observation = protocol.Observation()
        with self.assertRaises(protocol.Failure) as error:
            protocol.parse_inventory_row('43 42 malicious-secret 42 501 hostile payload x y z command',
                                         (identity,) * 3, observation)
        self.assertEqual(error.exception.category, 'malformed_inventory')
        body = json.dumps(observation.events)
        self.assertNotIn('malicious-secret', body)
        self.assertNotIn('hostile', body)
        fields = {item['field']: item for item in observation.events[0]['fields']}
        self.assertEqual(fields['pgid']['state'], 'unknown')
        self.assertIsNone(fields['pgid']['observed'])
        self.assertEqual(fields['pgid']['observed_type'], 'unparseable_text')
        self.assertEqual(fields['pgid']['observed_length'], len('malicious-secret'))
        self.assertIsNone(fields['start_sec']['observed'])
        observation = protocol.Observation()
        with self.assertRaises(protocol.Failure):
            protocol.parse_inventory_row('43 42 abc 42 501 Mon Jan 01 12:00:00 2024 command',
                                         (identity,) * 3, observation)
        fields = {item['field']: item for item in observation.events[0]['fields']}
        self.assertEqual(fields['start_sec']['observed'], 'Mon Jan 01 12:00:00 2024')

    def test_alarm_at_handoff_still_finalizes_registered_group(self):
        class Interrupted(dict):
            def __getitem__(self, key):
                raise TimeoutError('alarm private detail')
        output, cleaned = [], []
        from unittest.mock import patch
        with patch.object(protocol, 'nested', return_value=Interrupted()):
            with self.assertRaises(protocol.ProtocolFailure):
                protocol.run(lambda register: register('owned'), valid_operation,
                             lambda handle: cleaned.append(handle) or cleanup_success(handle), output.append)
        self.assertEqual(cleaned, ['owned'])
        self.assertEqual(json.loads(output[0])['failure'], {'phase': 'launch', 'category': 'timeout'})
        self.assertNotIn('private detail', output[0].decode())

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
