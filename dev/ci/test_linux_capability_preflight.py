"""Synthetic byte and hostname decisions; no guest execution or identity changes."""
import base64
import builtins
import hashlib
import io
import json
import sys
import unittest
from contextlib import redirect_stdout
from unittest.mock import patch

import linux_capability_preflight as subject


class PreflightTest(unittest.TestCase):
    def test_lf_crlf_and_independent_mismatches(self):
        data = b'first\nsecond\n'
        digest = hashlib.sha256(data).hexdigest()
        for payload, host, bytes_match, host_match in (
            (data, subject.EXPECTED_HOST, True, True),
            (b'First\nsecond\n', subject.EXPECTED_HOST, False, True),
            (data, 'other-host', True, False),
            (b'wrong', 'other-host', False, False),
        ):
            with self.subTest(host=host, payload=payload):
                result = subject.inspect(base64.b64encode(payload).decode(), len(data), digest, host)
                self.assertEqual((result['collector_match'], result['hostname_match']), (bytes_match, host_match))
                self.assertEqual(result['decoded_sha256'], hashlib.sha256(payload).hexdigest())
        crlf = data.replace(b'\n', b'\r\n')
        result = subject.inspect(base64.b64encode(crlf).decode(), len(data), digest, subject.EXPECTED_HOST)
        self.assertFalse(result['collector_match'])
        self.assertEqual(result['decoded_length'], len(crlf))

    def test_malformed_and_oversize(self):
        digest = hashlib.sha256(b'x').hexdigest()
        self.assertEqual(subject.inspect('!', 1, digest, 'host')['error'], 'invalid_base64')
        self.assertEqual(subject.inspect('a' * (subject.MAX_ENCODED + 1), 1, digest, 'host')['error'], 'encoded_size')
        with self.assertRaises(ValueError):
            subject.inspect('eA==', 1, 'bad', 'host')

    def test_collector_bytes_are_never_executed(self):
        payload = b'raise AssertionError("executed")'
        with patch('builtins.exec', side_effect=AssertionError('exec')), \
                patch('os.setuid', side_effect=AssertionError('identity'), create=True), \
                patch('os.setgid', side_effect=AssertionError('identity'), create=True):
            for digest, host in ((hashlib.sha256(payload).hexdigest(), subject.EXPECTED_HOST),
                                 ('0' * 64, subject.EXPECTED_HOST),
                                 (hashlib.sha256(payload).hexdigest(), 'other'), ('0' * 64, 'other')):
                subject.inspect(base64.b64encode(payload).decode(), len(payload), digest, host)

    def test_main_reports_without_executing_or_dropping_identity(self):
        payload = b'import collector_sentinel\nraise AssertionError("executed")\n'
        digest = hashlib.sha256(payload).hexdigest()
        encoded = base64.b64encode(payload).decode()
        crlf = base64.b64encode(payload.replace(b'\n', b'\r\n')).decode()
        cases = (('lf-match', encoded, digest, subject.EXPECTED_HOST, True, True, None),
                 ('crlf', crlf, digest, subject.EXPECTED_HOST, False, True, None),
                 ('malformed', '!', digest, subject.EXPECTED_HOST, False, True, 'invalid_base64'),
                 ('oversize', 'a' * (subject.MAX_ENCODED + 1), digest, subject.EXPECTED_HOST, False, True, 'encoded_size'),
                 ('hash-only', encoded, '0' * 64, subject.EXPECTED_HOST, False, True, None),
                 ('host-only', encoded, digest, 'other', True, False, None),
                 ('both', encoded, '0' * 64, 'other', False, False, None))
        real_import = builtins.__import__
        def guarded_import(name, *args, **kwargs):
            if name == 'collector_sentinel':
                raise AssertionError('collector import')
            return real_import(name, *args, **kwargs)
        for label, data, expected_hash, host, bytes_match, host_match, error in cases:
            output = io.StringIO()
            args = ['helper', data, str(len(payload)), expected_hash]
            with self.subTest(label=label), patch.object(sys, 'argv', args), \
                 patch.object(subject.socket, 'gethostname', return_value=host), \
                 patch('builtins.exec', side_effect=AssertionError('exec')), \
                 patch('builtins.__import__', side_effect=guarded_import), \
                 patch('os.setuid', side_effect=AssertionError('identity'), create=True), \
                 patch('os.setgid', side_effect=AssertionError('identity'), create=True), redirect_stdout(output):
                subject.main()
            result = json.loads(output.getvalue())
            self.assertEqual((result['collector_match'], result['hostname_match'], result['error']),
                             (bytes_match, host_match, error))


if __name__ == '__main__':
    unittest.main()
