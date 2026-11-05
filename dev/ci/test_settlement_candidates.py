"""Offline tests for bounded inventory candidate diagnostics."""
import hashlib
import unittest
from unittest.mock import Mock

import settlement_candidates as candidates


def row(pid, uid='501', suffix='Mon Jan 01 00:00:00 2024 private-command'):
    return f'{pid} 1 2 0 {uid} {suffix}\n'.encode()


IDENTITIES = ({'pid': 42, 'uid': 501}, {'pid': 43, 'uid': 501}, {'pid': 44, 'uid': 501})


class CandidateTests(unittest.TestCase):
    def test_absent_matching_and_excluded(self):
        body = row(42) + row(43, '502')
        result = candidates.scan(body, IDENTITIES, True)
        self.assertEqual(result['original_bytes'], len(body))
        self.assertEqual(result['sha256'], hashlib.sha256(body).hexdigest())
        self.assertTrue(result['scan_complete'])
        self.assertEqual([result['roles'][name]['classification'] for name in candidates.ROLES],
                         ['uid_filter_match', 'uid_filter_excluded', 'absent'])
        self.assertEqual(result['roles']['command']['summaries'][0]['uid'], 502)

    def test_missing_malformed_and_shape_fail_closed(self):
        for body in (b'43 1 2 0\n', row(43, 'not-a-uid'), b'43 1 2 0 501\n'):
            with self.subTest(body=body):
                result = candidates.scan(body, IDENTITIES, True)['roles']['command']
                self.assertEqual(result['classification'], 'incomplete')
                self.assertNotIn('not-a-uid', str(result))
        self.assertEqual(candidates.scan(b'43 1 2 0\n', IDENTITIES, True)['roles']['command']
                         ['summaries'][0]['uid_status'], 'missing')

    def test_duplicates_are_bounded_and_ambiguous(self):
        result = candidates.scan(row(43) + row(43, '502') + row(43), IDENTITIES, True)['roles']['command']
        self.assertEqual((result['classification'], result['count'], result['excess_count']), ('ambiguous', 3, 1))
        self.assertEqual([item['uid'] for item in result['summaries']], [501, 502])

    def test_bad_first_field_and_output_bound_are_incomplete(self):
        for body, complete in ((b'bad-token 1 2 0 501 private\n' + row(43), True),
                               (row(43), False), (b'x' * (candidates.MAX_INVENTORY + 1), True)):
            result = candidates.scan(body, IDENTITIES, complete)
            self.assertFalse(result['scan_complete'])
            self.assertTrue(all(item['classification'] == 'incomplete' for item in result['roles'].values()))
            self.assertNotIn('bad-token', str(result))

    def test_unicode_tokenization_matches_original_parser(self):
        body = '٤٣\u00a0 1 2 0 ٥٠١ Mon Jan 01 00:00:00 2024 private\n'.encode('utf-8')
        result = candidates.scan(body, IDENTITIES, True)
        self.assertTrue(result['scan_complete'])
        self.assertEqual(result['roles']['command']['classification'], 'uid_filter_match')
        self.assertEqual(result['roles']['command']['summaries'][0]['uid'], 501)
        self.assertEqual(result['sha256'], hashlib.sha256(body).hexdigest())

    def test_unbounded_uid_is_fixed_malformed_without_raw_value(self):
        result = candidates.scan(row(43, '9223372036854775808'), IDENTITIES, True)['roles']['command']
        self.assertEqual(result['classification'], 'incomplete')
        self.assertEqual(result['summaries'][0]['uid_status'], 'malformed')
        self.assertIsNone(result['summaries'][0]['uid'])
        self.assertNotIn('9223372036854775808', str(result))

    def test_unrelated_and_command_text_never_survive(self):
        result = candidates.scan(row(999, suffix='private path and command text here') + row(43), IDENTITIES, True)
        self.assertNotIn('private', str(result))
        self.assertNotIn('999', str(result))
        self.assertEqual(result['roles']['command']['classification'], 'uid_filter_match')

    def test_invalid_role_identity_is_rejected(self):
        for identities in (({'pid': True, 'uid': 501}, *IDENTITIES[1:]),
                           (IDENTITIES[0], IDENTITIES[0], IDENTITIES[2])):
            with self.assertRaises(ValueError):
                candidates.scan(b'', identities, True)

    def test_record_separates_diagnostic_errors_but_not_deadlines(self):
        observation = Mock()
        candidates.record(observation, b'', IDENTITIES, True, Mock(side_effect=ValueError('private')))
        self.assertEqual(observation.candidate_diagnostic, {'kind': 'inventory_candidates', 'diagnostic': 'unavailable'})
        with self.assertRaises(TimeoutError):
            candidates.record(observation, b'', IDENTITIES, True, Mock(side_effect=TimeoutError('deadline')))
        with self.assertRaises(KeyboardInterrupt):
            candidates.record(observation, b'', IDENTITIES, True, Mock(side_effect=KeyboardInterrupt()))

    def test_labels_describe_uid_filter_only_not_full_parser(self):
        bad_parent = b'43 bad-parent 2 0 502 Mon Jan 01 00:00:00 2024 private\n'
        bad_start = b'43 1 2 0 501 bad date values here now private\n'
        self.assertEqual(candidates.scan(bad_parent, IDENTITIES, True)['roles']['command']['classification'],
                         'uid_filter_excluded')
        self.assertEqual(candidates.scan(bad_start, IDENTITIES, True)['roles']['command']['classification'],
                         'uid_filter_match')

    def test_qualification_distinguishes_absent_role_from_absent_diagnostic(self):
        diagnostic = candidates.scan(b'', IDENTITIES, True)
        self.assertEqual(candidates.qualification(diagnostic), 'complete')
        self.assertEqual(candidates.qualify_records([]), 'FAIL')
        self.assertEqual(candidates.qualify_records([{'record_truncated': False, 'candidate_diagnostic': diagnostic,
                                                       'diagnostic_qualification': 'complete'}]), 'PASS')
        diagnostic['scan_complete'] = False
        self.assertEqual(candidates.qualify_records([{'record_truncated': False, 'candidate_diagnostic': diagnostic,
                                                       'diagnostic_qualification': 'complete'}]), 'FAIL')


if __name__ == '__main__':
    unittest.main()
