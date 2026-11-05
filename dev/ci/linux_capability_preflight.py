"""Report collector byte identity and hostname without executing the collector."""
import base64
import binascii
import hashlib
import json
import re
import socket
import sys

MAX_COLLECTOR = 65536
MAX_ENCODED = 4 * ((MAX_COLLECTOR + 2) // 3)
EXPECTED_HOST = 'ubuntu-runner-201'


def inspect(encoded, expected_length, expected_hash, observed_host):
    result = {'expected_hostname': EXPECTED_HOST, 'observed_hostname': observed_host,
              'hostname_match': observed_host == EXPECTED_HOST,
              'decoded_length': None, 'decoded_sha256': None, 'collector_match': False,
              'error': None}
    if (type(expected_length) is not int or not 0 < expected_length <= MAX_COLLECTOR or
            not re.fullmatch('[0-9a-f]{64}', expected_hash)):
        raise ValueError('invalid expected collector identity')
    if not isinstance(encoded, str) or len(encoded) > MAX_ENCODED:
        result['error'] = 'encoded_size'
        return result
    try:
        data = base64.b64decode(encoded, validate=True)
    except (ValueError, binascii.Error):
        result['error'] = 'invalid_base64'
        return result
    if len(data) > MAX_COLLECTOR:
        result['error'] = 'decoded_size'
        return result
    result['decoded_length'] = len(data)
    result['decoded_sha256'] = hashlib.sha256(data).hexdigest()
    result['collector_match'] = len(data) == expected_length and result['decoded_sha256'] == expected_hash
    return result


def main():
    if len(sys.argv) != 4:
        raise ValueError('expected encoded bytes, length and digest')
    hostname = socket.gethostname()
    if len(hostname.encode('utf-8')) > 255:
        raise ValueError('hostname exceeds bound')
    result = inspect(sys.argv[1], int(sys.argv[2]), sys.argv[3], hostname)
    print(json.dumps(result, sort_keys=True), flush=True)


if __name__ == '__main__':
    main()
