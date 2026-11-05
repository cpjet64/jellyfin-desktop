"""Bounded own-PID candidate summaries from an already collected inventory buffer."""
import hashlib
import json
import re

MAX_INVENTORY = 1024 * 1024
ROLES = ('leader', 'command', 'probe')
IDENTITY_COMPARISON_FIELDS = ('pid', 'ppid', 'uid', 'pgid', 'session', 'start_sec')


def identity_comparison(expected, observed, problem=None):
    """Describe the existing ordered, short-circuit comparison without changing it."""
    result, stopped = [], problem is not None
    for name in IDENTITY_COMPARISON_FIELDS:
        left, right = expected.get(name), observed.get(name)
        state = 'not_evaluated'
        if problem == name:
            state = 'unknown'
        elif not stopped:
            if left is None or right is None:
                state, stopped = 'unknown', True
            elif left == right:
                state = 'match'
            else:
                state, stopped = 'mismatch', True
        result.append({'field': name, 'state': state, 'expected': left, 'observed': right,
                       'expected_unit': 'seconds_since_epoch' if name == 'start_sec' else 'integer',
                       'observed_unit': ('text' if isinstance(right, str) else
                                         'seconds_since_epoch' if name == 'start_sec' else 'integer'),
                       'expected_source': 'getsid' if name == 'session' else 'kernel_identity',
                       'observed_source': ('ps_lstart_raw' if name == 'start_sec' and isinstance(right, str)
                                           else 'ps_lstart_normalized' if name == 'start_sec' else 'ps')})
    return {'kind': 'identity_comparison', 'fields': result}


def integer(value):
    return type(value) is int and -(2**63) <= value < 2**63


def fields(values, allowed):
    if not isinstance(values, dict) or not set(values) <= allowed:
        raise ValueError('unexpected observation fields')
    if any(value is not None and not integer(value) and type(value) is not bool for value in values.values()):
        raise ValueError('invalid observation value')
    return dict(values)


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
    if uid is not None and not integer(uid):
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


def valid_attempt_id(value):
    return isinstance(value, str) and re.fullmatch(r'[0-9a-f]{32}', value, flags=re.ASCII) is not None


def qualify_records(records, expected_attempt_id=None):
    if not isinstance(records, list):
        return 'FAIL'
    if any(isinstance(r, dict) and r.get('claim', 'settlement') != 'settlement' for r in records):
        return 'FAIL'
    if expected_attempt_id is not None and (not valid_attempt_id(expected_attempt_id) or
            any(not isinstance(r, dict) or r.get('attempt_id') != expected_attempt_id for r in records)):
        return 'FAIL'
    found = [item for item in records if isinstance(item, dict) and item.get('candidate_diagnostic') is not None]
    if len(found) != 1:
        return 'FAIL'
    record = found[0]
    if record.get('claim', 'settlement') != 'settlement':
        return 'FAIL'
    try:
        return 'PASS' if (record.get('record_truncated') is False and
                          record.get('diagnostic_qualification') == 'complete' and
                          qualification(record['candidate_diagnostic']) == 'complete') else 'FAIL'
    except TimeoutError:
        return 'FAIL'


def qualify_observation_records(records, expected_attempt_id=None):
    """Accept only an explicitly bounded, successful metadata observation."""
    if (not valid_attempt_id(expected_attempt_id) or not isinstance(records, list) or not records or
            any(not isinstance(r, dict) or r.get('attempt_id') != expected_attempt_id for r in records)):
        return 'FAIL'
    if any(not valid_observation_record(record) for record in records):
        return 'FAIL'
    inventory = [record for record in records if record.get('purpose') == 'inventory']
    if len(inventory) != 1:
        return 'FAIL'
    try:
        record = inventory[0]
        return 'PASS' if (record.get('diagnostic_qualification') == 'complete' and
                          qualification(record.get('candidate_diagnostic')) == 'complete') else 'FAIL'
    except TimeoutError:
        return 'FAIL'


def valid_observation_record(record):
    purpose = record.get('purpose')
    if (not valid_observation_envelope(record, nested=False) or
            record.get('claim') != 'metadata_observation' or record.get('coverage') != 'bounded' or
            purpose not in ('command', 'inventory') or
            record.get('ok') is not True or record.get('failure') is not None or
            record.get('secondary') != [] or record.get('record_truncated') is not False or
            not integer(record.get('elapsed_ms')) or record['elapsed_ms'] < 0):
        return False
    events, cleanup = record.get('events'), record.get('cleanup')
    if not isinstance(events, list) or not isinstance(cleanup, list) or not cleanup:
        return False
    if not valid_observation_events(events, purpose):
        return False
    if any(not isinstance(c, dict) or set(c) - {'requested', 'operation', 'leader_reap', 'group_settlement', 'errors'} or
           c.get('requested') is not True or c.get('errors') not in (None, []) or
           any(c.get(k) != 'success' for k in ('operation', 'leader_reap', 'group_settlement')) for c in cleanup):
        return False
    return purpose == 'inventory' or (record.get('candidate_diagnostic') is None and
                                      record.get('diagnostic_qualification') == 'missing')


def valid_observation_envelope(record, nested):
    """Reject unrecognized or missing claim-bearing envelope fields."""
    if not isinstance(record, dict):
        return False
    common = {'ok', 'failure', 'events', 'claim', 'purpose', 'coverage', 'attempt_id'}
    required = common | ({'value', 'candidate_diagnostic'} if nested else
                         {'secondary', 'cleanup', 'elapsed_ms', 'record_truncated', 'diagnostic_qualification'})
    optional = {'secondary'} if nested else {'candidate_diagnostic'}
    if (not required <= set(record) or set(record) - required - optional or type(record['ok']) is not bool or
            not valid_attempt_id(record['attempt_id'])):
        return False
    if nested and record['ok']:
        return record['failure'] is None and isinstance(record['value'], dict) and record.get('secondary', []) == []
    if nested:
        return record['value'] is None and isinstance(record['failure'], dict) and isinstance(record.get('secondary', []), list)
    return True


def valid_observation_events(events, purpose):
    """Validate existing event evidence without resolving or rereading any identity."""
    if not isinstance(events, list):
        return False
    command = leader = False
    subjects = 0
    owned_pids, unrelated_sets, comparisons, identity_pids = set(), [], {}, {}
    for event in events:
        if not isinstance(event, dict):
            return False
        kind = event.get('kind')
        if kind == 'command':
            values = event.get('values')
            allowed = {'exit', 'signal', 'elapsed_ms', 'output_bytes', 'retained_bytes', 'output_bound', 'truncated'}
            role = event.get('role')
            if role == 'subject':
                subjects += 1
            if (not {'kind', 'phase', 'values'} <= set(event) or set(event) - {'kind', 'phase', 'values', 'role'} or
                    ('role' in event and (role != 'subject' or purpose != 'command' or event.get('phase') != 'inventory')) or
                    event.get('phase') not in {'preparation', 'launch', 'held_identity', 'inventory', 'identification', 'cleanup'} or
                    not isinstance(values, dict) or set(values) - allowed or
                    any(not (v is None or integer(v) or type(v) is bool) for v in values.values()) or
                    any(k in values and type(values[k]) is not bool for k in ('output_bound', 'truncated')) or
                    any(k in values and values[k] is not None and not integer(values[k]) for k in allowed - {'output_bound', 'truncated'}) or
                    not integer(values.get('exit')) or values['exit'] < 0 or
                    (values['exit'] != 0 and role != 'subject') or
                    (values.get('signal') is not None and (not integer(values['signal']) or values['signal'] != 0))
                    or values.get('truncated') is True or values.get('output_bound') is True):
                return False
            command = True
        elif kind == 'identity':
            role, state, values = event.get('role'), event.get('state'), event.get('values')
            if (role not in ('own_probe', 'own_leader', 'own_command', 'observed_row') or
                    state not in ('success', 'failed', 'unknown', 'not_attempted', 'disappeared', 'mismatch') or
                    set(event) - {'kind', 'role', 'state', 'api', 'values', 'unrelated_to_pids'} or
                    event.get('api') not in (None, 'bsdinfo_before', 'bsdinfo_after', 'path_before', 'path_after') or
                    not isinstance(values, dict) or set(values) - {'pid', 'uid', 'ppid', 'pgid', 'session',
                        'start_sec', 'start_usec', 'api_result', 'api_error', 'expected_size', 'returned_size'} or
                    any(not (v is None or integer(v) or type(v) is bool) for v in values.values())):
                return False
            if role == 'own_leader' and state == 'success':
                leader = True
            if role.startswith('own_') and state == 'success':
                pid = values.get('pid')
                if not integer(pid) or pid <= 0:
                    return False
                owned_pids.add(pid)
                if role in identity_pids and identity_pids[role] != pid:
                    return False
                identity_pids[role] = pid
            if state != 'success':
                pids = event.get('unrelated_to_pids')
                pid = values.get('pid')
                if (role != 'observed_row' or not integer(pid) or pid <= 0 or not isinstance(pids, list)
                        or len(pids) != 3 or any(not integer(p) or p <= 0 for p in pids)
                        or len(set(pids)) != 3 or pid in pids):
                    return False
                unrelated_sets.append(set(pids))
        elif kind == 'identity_comparison':
            role = event.get('role')
            pid = comparison_pid(event)
            if pid is None or role in comparisons:
                return False
            comparisons[role] = pid
        else:
            return False
    registered = set(comparisons.values())
    if purpose == 'command':
        return command and leader and subjects == 1 and not comparisons and not unrelated_sets
    return (purpose == 'inventory' and command and leader and len(comparisons) == 3 and len(registered) == 3 and
            all(comparisons.get(role) == pid for role, pid in identity_pids.items()) and
            owned_pids <= registered and all(pids == registered for pids in unrelated_sets))


def comparison_pid(event):
    role, values = event.get('role'), event.get('fields')
    names = ('pid', 'ppid', 'uid', 'pgid', 'session', 'start_sec')
    scope = 'historical_completed_not_current_ownership' if role == 'own_command' else 'held'
    if (role not in ('own_probe', 'own_leader', 'own_command') or event.get('scope') != scope or
            set(event) != {'kind', 'fields', 'role', 'ps_session_metadata', 'scope'} or
            not isinstance(values, list) or len(values) != 6):
        return None
    for name, value in zip(names, values):
        minimum = 1 if name in ('pid', 'pgid', 'session') else 0
        if (not isinstance(value, dict) or value.get('field') != name or not integer(value.get('expected')) or
                value['expected'] < minimum):
            return None
        if name == 'session' and role == 'own_command':
            if value.get('state') != 'not_applicable' or value.get('observed') is not None:
                return None
        elif (value.get('state') != 'match' or not integer(value.get('observed')) or
              value['observed'] != value['expected']):
            return None
    return values[0]['expected'] if values[0]['expected'] > 0 else None
