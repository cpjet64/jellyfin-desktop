"""Bounded own-PID candidate summaries from an already collected inventory buffer."""
import hashlib
import json
import re

MAX_INVENTORY = 1024 * 1024
ROLES = ('leader', 'command', 'probe')


def parsed_integer(token):
    try:
        value = int(token)
    except (ValueError, TypeError):
        return None
    return value


def candidate_summary(parts, expected_uid):
    shape = 'valid' if len(parts) == 11 else 'invalid'
    if len(parts) <= 4:
        status, uid = 'missing', None
    else:
        uid = parsed_integer(parts[4])
        status = 'valid' if uid is not None else 'malformed'
    matches_filter = uid == 501 if uid is not None else None
    matches_expected = uid == expected_uid if uid is not None else None
    if uid is not None and not -(2**63) <= uid < 2**63:
        status, uid, matches_filter, matches_expected = 'malformed', None, None, None
    return {'shape': shape, 'uid_status': status, 'uid': uid,
            'matches_filter': matches_filter, 'matches_expected': matches_expected}


def classification(summaries, count, complete):
    if not complete:
        return 'incomplete'
    if count == 0:
        return 'absent'
    if count > 1:
        return 'ambiguous'
    item = summaries[0]
    if item['shape'] != 'valid' or item['uid_status'] != 'valid':
        return 'incomplete'
    if not item['matches_filter']:
        return 'uid_filter_excluded'
    return 'uid_filter_match' if item['matches_expected'] else 'incomplete'


def scan(buffer, identities, complete):
    """Return bounded metadata only; never retain unrelated or raw row data."""
    if (not isinstance(buffer, bytes) or type(complete) is not bool or
            not isinstance(identities, (list, tuple)) or len(identities) != 3 or
            any(not isinstance(item, dict) for item in identities)):
        raise ValueError('invalid candidate input')
    ids = [item.get('pid') for item in identities]
    uids = [item.get('uid') for item in identities]
    if (any(type(value) is not int or not 0 < value < 2**63 for value in ids) or len(set(ids)) != 3 or
            any(type(value) is not int or not 0 <= value < 2**63 for value in uids)):
        raise ValueError('invalid candidate identities')
    counts = [0, 0, 0]
    summaries = [[], [], []]
    scan_complete = complete and len(buffer) <= MAX_INVENTORY
    if scan_complete:
        for line in buffer.decode('utf-8', 'replace').splitlines():
            parts = line.split(None, 10)
            pid = parsed_integer(parts[0]) if parts else None
            if pid is None:
                scan_complete = False
                continue
            if pid in ids:
                index = ids.index(pid)
                counts[index] += 1
                if len(summaries[index]) < 2:
                    summaries[index].append(candidate_summary(parts, uids[index]))
    result = {}
    for index, role in enumerate(ROLES):
        result[role] = {'classification': classification(summaries[index], counts[index], scan_complete),
                        'count': counts[index], 'summaries': summaries[index],
                        'excess_count': max(0, counts[index] - len(summaries[index]))}
    return {'kind': 'inventory_candidates', 'original_bytes': len(buffer),
            'sha256': hashlib.sha256(buffer).hexdigest(), 'scan_complete': scan_complete, 'roles': result}


def record(observation, buffer, identities, complete, validate):
    """Keep diagnostic errors separate without intercepting deadline cancellation."""
    try:
        validate(identities)
        event = scan(buffer, identities, complete)
    except TimeoutError:
        raise
    except Exception:
        event = {'kind': 'inventory_candidates', 'diagnostic': 'unavailable'}
    try:
        observation.candidate_diagnostic = event
    except TimeoutError:
        raise
    except Exception:
        pass


def valid_summary(item):
    keys = {'shape', 'uid_status', 'uid', 'matches_filter', 'matches_expected'}
    if not isinstance(item, dict) or set(item) != keys or item['shape'] not in ('valid', 'invalid'):
        return False
    status, uid = item['uid_status'], item['uid']
    if status == 'valid':
        return (type(uid) is int and -(2**63) <= uid < 2**63 and
                type(item['matches_filter']) is bool and item['matches_filter'] == (uid == 501) and
                type(item['matches_expected']) is bool)
    return status in ('missing', 'malformed') and uid is None and item['matches_filter'] is None and item['matches_expected'] is None


def qualification(value):
    """A missing, oversized or incomplete diagnostic is never qualified."""
    if value is None:
        return 'missing'
    if not isinstance(value, dict):
        return 'unavailable'
    try:
        if len(json.dumps(value, separators=(',', ':')).encode('utf-8')) > 4096:
            return 'unavailable'
        if value == {'kind': 'inventory_candidates', 'diagnostic': 'unavailable'}:
            return 'unavailable'
        roles = value.get('roles')
        if (set(value) != {'kind', 'original_bytes', 'sha256', 'scan_complete', 'roles'} or
                value.get('kind') != 'inventory_candidates' or not isinstance(roles, dict) or set(roles) != set(ROLES) or
                type(value.get('scan_complete')) is not bool):
            return 'unavailable'
        if (type(value.get('original_bytes')) is not int or not 0 <= value['original_bytes'] <= MAX_INVENTORY or
                not isinstance(value.get('sha256'), str) or not re.fullmatch('[0-9a-f]{64}', value['sha256'])):
            return 'unavailable'
        allowed = ('absent', 'uid_filter_match', 'uid_filter_excluded', 'ambiguous', 'incomplete')
        for name in ROLES:
            item = roles[name]
            if not isinstance(item, dict) or set(item) != {'classification', 'count', 'summaries', 'excess_count'}:
                return 'unavailable'
            count, summaries = item.get('count'), item.get('summaries')
            if (type(count) is not int or count < 0 or not isinstance(summaries, list) or
                    len(summaries) != min(count, 2) or type(item.get('excess_count')) is not int or
                    item['excess_count'] != max(0, count - 2) or
                    any(not valid_summary(summary) for summary in summaries) or
                    item.get('classification') not in allowed or
                    item['classification'] != classification(summaries, count, value['scan_complete'])):
                return 'unavailable'
        if not value['scan_complete'] or any(roles[name]['classification'] == 'incomplete' for name in ROLES):
            return 'incomplete'
        return 'complete'
    except TimeoutError:
        raise
    except Exception:
        return 'unavailable'


def qualify_records(records):
    if not isinstance(records, list):
        return 'FAIL'
    found = [item for item in records if isinstance(item, dict) and item.get('candidate_diagnostic') is not None]
    if len(found) != 1:
        return 'FAIL'
    record = found[0]
    try:
        return 'PASS' if (record.get('record_truncated') is False and
                          record.get('diagnostic_qualification') == 'complete' and
                          qualification(record['candidate_diagnostic']) == 'complete') else 'FAIL'
    except TimeoutError:
        return 'FAIL'
