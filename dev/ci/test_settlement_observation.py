"""Offline tests of the actual per-attempt classification and emitted records."""
import json
import unittest
from unittest.mock import patch

import settlement_candidates as candidates
import settlement_failure as protocol


def identities():
    return tuple(dict(pid=pid, ppid=1, uid=501, pgid=pid, session=pid,
                      start_sec=1, start_usec=0) for pid in (20, 21, 22))


def cleanup(_):
    return dict(requested=True, operation='success', leader_reap='success', group_settlement='success')


def child_frame(policy, role='observed_row', state='unknown', pid=40, ids=None, problem=None):
    child = protocol.Observation(policy, 'inventory' if policy == 'metadata_observation' else None)
    def operation(obs):
        obs.command('inventory', {'exit': 0})
        obs.identity('own_leader', 'success', {'pid': 20})
        for owned_role, expected in zip(('leader', 'command', 'probe'), identities()):
            row = {key: expected[key] for key in ('pid', 'ppid', 'uid', 'pgid')}
            held = {key: expected[key] for key in ('pid', 'ppid', 'uid', 'pgid', 'start_sec', 'start_usec')}
            protocol.compare_owned_row(owned_role, expected, row, 1, obs, lambda _, held=held: held,
                                       lambda _, expected=expected: expected['session'])
        obs.identity(role, state, {'pid': pid, 'uid': 501}, registered_identities=ids)
        if problem:
            problem(obs)
        obs.candidate_diagnostic = candidates.scan(b'', identities(), True)
        return {'sampled': True}
    frame = protocol.nested(operation, child)
    frame['events'] = child.events
    return frame


def capture(frame, policy='metadata_observation', cleanup_fn=cleanup):
    emitted = []
    try:
        value = protocol.run(lambda register: register('owned'),
                             lambda obs: protocol.import_nested_frame(frame, obs),
                             cleanup_fn, emitted.append, clock=lambda: 1, policy=policy,
                             purpose='inventory' if policy == 'metadata_observation' else None)
    except protocol.ProtocolFailure as error:
        value = error
    return json.loads(emitted[0]), value


class ObservationPolicyTests(unittest.TestCase):
    def test_unrelated_uncertainty_through_nested_parent_and_consumers(self):
        for state in ('unknown', 'disappeared', 'mismatch'):
            with self.subTest(state=state):
                record, result = capture(child_frame('metadata_observation', state=state, ids=identities()))
                self.assertEqual(result, {'sampled': True})
                self.assertEqual((record['claim'], record['coverage']), ('metadata_observation', 'bounded'))
                self.assertEqual(record['events'][-1]['state'], state)
                self.assertEqual(candidates.qualify_observation_records([record]), 'PASS')
                self.assertEqual(candidates.qualify_records([record]), 'FAIL')

    def test_aggregate_has_one_inventory_and_bounded_command_records(self):
        child = protocol.Observation('metadata_observation', 'command')
        def operation(obs):
            obs.identity('own_leader', 'success', {'pid': 20})
            obs.command('inventory', {'exit': 0})
            return {'sampled': True}
        frame = protocol.nested(operation, child)
        frame['events'] = child.events
        emitted = []
        protocol.run(lambda register: register('owned'), lambda obs: protocol.import_nested_frame(frame, obs),
                     cleanup, emitted.append, clock=lambda: 1, policy='metadata_observation', purpose='command')
        command = json.loads(emitted[0])
        inventory, _ = capture(child_frame('metadata_observation', ids=identities()))
        self.assertEqual(candidates.qualify_observation_records([command, inventory, command]), 'PASS')
        nonzero = dict(frame, events=[dict(e, values={'exit': 3}) if e['kind'] == 'command' else e
                                      for e in frame['events']])
        emitted.clear()
        protocol.run(lambda register: register('owned'), lambda obs: protocol.import_nested_frame(nonzero, obs),
                     cleanup, emitted.append, clock=lambda: 1, policy='metadata_observation', purpose='command')
        self.assertEqual(candidates.qualify_observation_records([json.loads(emitted[0]), inventory]), 'PASS')
        self.assertEqual(candidates.qualify_observation_records([command]), 'FAIL')
        self.assertEqual(candidates.qualify_observation_records([inventory, inventory]), 'FAIL')
        self.assertEqual(candidates.qualify_observation_records([command, dict(inventory, purpose='changed')]), 'FAIL')
        self.assertEqual(candidates.qualify_observation_records([dict(command, coverage='incomplete'), inventory]), 'FAIL')

    def test_owned_ambiguous_and_unregistered_remain_failed(self):
        for role, pid, ids in (('own_probe', 20, identities()), ('observed_row', 20, identities()),
                               ('observed_row', 40, None), ('observed_row', 40, identities()[:2]),
                               ('observed_row', 0, identities())):
            with self.subTest(role=role, pid=pid, ids=ids):
                record, error = capture(child_frame('metadata_observation', role=role, pid=pid, ids=ids))
                self.assertIsInstance(error, protocol.ProtocolFailure)
                self.assertEqual(record['coverage'], 'incomplete')
                self.assertEqual(record['failure']['category'], 'identity_unavailable')
                self.assertEqual(candidates.qualify_observation_records([record]), 'FAIL')

    def test_settlement_unchanged_and_interleaved_policies(self):
        first = protocol.Observation('metadata_observation', 'inventory')
        second = protocol.Observation()
        first.identity('observed_row', 'unknown', {'pid': 40, 'uid': 501}, registered_identities=identities())
        second.identity('observed_row', 'unknown', {'pid': 40, 'uid': 501}, registered_identities=identities())
        self.assertIsNone(first.qualification_failure)
        self.assertEqual(second.qualification_failure['category'], 'identity_unavailable')
        self.assertEqual(len(first.events), 1)
        record, error = capture(child_frame('settlement', ids=identities()), policy='settlement')
        self.assertIsInstance(error, protocol.ProtocolFailure)
        self.assertNotIn('claim', record)

    def test_claim_fail_closed_and_legacy_settlement(self):
        frame = child_frame('metadata_observation', ids=identities())
        for claim in (None, 'settlement', 'altered'):
            altered = dict(frame, claim=claim)
            record, error = capture(altered)
            self.assertIsInstance(error, protocol.ProtocolFailure)
            self.assertEqual(record['coverage'], 'incomplete')
        record, error = capture(frame, policy='settlement')
        self.assertIsInstance(error, protocol.ProtocolFailure)
        self.assertEqual(record['failure']['category'], 'malformed_inventory')
        legacy = {'record_truncated': False, 'candidate_diagnostic': candidates.scan(b'', identities(), True),
                  'diagnostic_qualification': 'complete'}
        self.assertEqual(candidates.qualify_records([legacy]), 'PASS')
        for claim in (None, 'changed', 'settlement'):
            self.assertEqual(candidates.qualify_observation_records([dict(legacy, claim=claim)]), 'FAIL')
        with self.assertRaises(ValueError):
            protocol.Observation('altered')

    def test_forged_success_frame_cannot_bypass_identity_or_command_failure(self):
        frame = child_frame('metadata_observation', ids=identities())
        for event in ({'kind': 'identity', 'role': 'own_probe', 'state': 'unknown', 'values': {'pid': 21}},
                      {'kind': 'identity', 'role': 'observed_row', 'state': 'unknown', 'values': {'pid': 40}},
                      {'kind': 'command', 'phase': 'inventory', 'values': {'exit': 1}},
                      {'kind': 'command', 'phase': 'inventory', 'values': {'exit': 0, 'truncated': 'yes'}},
                      {'kind': 'command', 'phase': 'inventory', 'values': {'exit': 0, 'output_bound': 1}},
                      {'kind': 'command', 'phase': 'wrong', 'values': {'exit': 0}},
                      {'kind': 'command', 'phase': 'inventory', 'values': {'exit': 0, 'extra': 1}},
                      {'kind': 'command', 'phase': 'inventory', 'values': {'exit': 0, 'truncated': True}}):
            with self.subTest(event=event):
                record, error = capture(dict(frame, events=frame['events'] + [event]))
                self.assertIsInstance(error, protocol.ProtocolFailure)
                self.assertEqual(record['coverage'], 'incomplete')

    def test_owned_comparison_failures_do_not_qualify(self):
        child = protocol.Observation('metadata_observation', 'inventory')
        expected = identities()[0]
        row = {'pid': 20, 'ppid': 2, 'uid': 501, 'pgid': 20}
        with self.assertRaises(protocol.Failure):
            protocol.compare_owned_row('leader', expected, row, 1, child, lambda _: None, lambda _: None)
        frame = child_frame('metadata_observation', ids=identities())
        record, error = capture(dict(frame, events=frame['events'] + child.events))
        self.assertIsInstance(error, protocol.ProtocolFailure)
        self.assertEqual(record['coverage'], 'incomplete')

    def test_missing_duplicate_and_malformed_owned_comparisons_fail(self):
        frame = child_frame('metadata_observation', ids=identities())
        events = frame['events']
        comparison = next(e for e in events if e['kind'] == 'identity_comparison')
        leader_removed = [e for e in events if e is not comparison]
        malformed = dict(comparison, fields=[dict(field, state='not_applicable') for field in comparison['fields']])
        bool_command = [dict(e, values={'exit': False}) if e['kind'] == 'command' else e for e in events]
        for modified in (leader_removed, events + [comparison], leader_removed + [malformed], bool_command):
            with self.subTest(modified=modified):
                record, error = capture(dict(frame, events=modified))
                self.assertIsInstance(error, protocol.ProtocolFailure)
                self.assertEqual(record['coverage'], 'incomplete')

    def test_failure_and_cleanup_are_not_overwritten(self):
        def fail(_):
            raise protocol.Failure('inventory', 'nonzero')
        bad_cleanup = lambda _: dict(requested=None, operation='unknown', leader_reap='unknown',
                                     group_settlement='unknown')
        record, error = capture(child_frame('metadata_observation', ids=identities(), problem=fail),
                                cleanup_fn=bad_cleanup)
        self.assertIsInstance(error, protocol.ProtocolFailure)
        self.assertEqual(record['failure'], {'phase': 'inventory', 'category': 'nonzero'})
        self.assertTrue(record['secondary'])
        self.assertEqual(record['coverage'], 'incomplete')

    def test_malformed_bounds_and_truncation_never_bounded_success(self):
        def malformed(obs):
            protocol.parse_inventory_row('bad', identities(), obs)
        def truncated(obs):
            obs.command('inventory', {'exit': 0, 'truncated': True})
        def capped(obs):
            with patch.object(protocol, 'MAX_EVENTS', len(obs.events)):
                obs.add({'kind': 'extra'})
        for fail in (malformed, truncated, capped):
            with self.subTest(fail=fail.__name__):
                record, error = capture(child_frame('metadata_observation', ids=identities(), problem=fail))
                self.assertIsInstance(error, protocol.ProtocolFailure)
                self.assertEqual(record['coverage'], 'incomplete')
        frame = child_frame('metadata_observation', ids=identities())
        with patch.object(protocol, 'MAX_RECORD', 300):
            record, error = capture(frame)
        self.assertIsInstance(error, protocol.ProtocolFailure)
        self.assertEqual((record['coverage'], record['record_truncated']), ('incomplete', True))


if __name__ == '__main__':
    unittest.main()
