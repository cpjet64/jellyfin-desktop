"""Bounded, transport-neutral failure records for supervised settlement checks.

This module performs no process, filesystem, network, or guest operations.
The caller supplies existing operations and a protected response-channel emitter.
"""
import json
import re
import time
import settlement_candidates as candidate_diagnostics

MAX_EVENTS = 4096
MAX_GROUPS = 8
MAX_RECORD = 1024 * 1024
PHASES = {'preparation', 'launch', 'held_identity', 'inventory', 'identification', 'cleanup'}
CATEGORIES = {'nonzero', 'timeout', 'output_bound', 'identity_unavailable', 'identity_mismatch',
              'malformed_inventory', 'cancellation', 'unknown'}
STATES = {'success', 'failed', 'unknown', 'not_attempted', 'disappeared', 'mismatch'}
ROLES = {'own_probe', 'own_leader', 'own_command', 'observed_row'}
APIS = {'bsdinfo_before', 'bsdinfo_after', 'path_before', 'path_after'}
IDENTITY_FIELDS = {'pid', 'uid', 'ppid', 'pgid', 'session', 'start_sec', 'start_usec',
                   'api_result', 'api_error', 'expected_size', 'returned_size'}
COMMAND_FIELDS = {'exit', 'signal', 'elapsed_ms', 'output_bytes', 'retained_bytes', 'output_bound', 'truncated'}
SECONDARY_DETAILS = ('fd_close_failed', 'mask_restore_failed', 'mask_block_failed')
IDENTITY_COMPARISON_FIELDS = ('pid', 'ppid', 'uid', 'pgid', 'session', 'start_sec')


class Failure(Exception):
    def __init__(self, phase, category, secondary=None):
        if phase not in PHASES or category not in CATEGORIES:
            raise ValueError('invalid failure classification')
        super().__init__(phase, category)
        self.phase, self.category = phase, category
        self.secondary = list(secondary or [])[:8]


class ProtocolFailure(RuntimeError):
    def __init__(self, record):
        super().__init__('supervised settlement operation failed')
        self.record = record


class DiagnosticFinalizationTimeout(TimeoutError):
    pass


def integer(value):
    return type(value) is int and -(2**63) <= value < 2**63


def fields(values, allowed):
    if not isinstance(values, dict) or not set(values) <= allowed:
        raise ValueError('unexpected observation fields')
    if any(value is not None and not integer(value) and type(value) is not bool for value in values.values()):
        raise ValueError('invalid observation value')
    return dict(values)


class Observation:
    def __init__(self):
        self.events = []
        self.groups = []
        self.qualification_failure = None
        self.candidate_diagnostic = None

    def add(self, event):
        if len(self.events) >= MAX_EVENTS:
            raise Failure('preparation', 'output_bound')
        self.events.append(event)

    def identity(self, role, state, values, api=None):
        if role not in ROLES or state not in STATES:
            raise ValueError('invalid identity observation')
        if api is not None and api not in APIS:
            raise ValueError('invalid identity API')
        self.add({'kind': 'identity', 'role': role, 'state': state, 'api': api,
                  'values': fields(values, IDENTITY_FIELDS)})
        if state != 'success' and self.qualification_failure is None:
            self.qualification_failure = {'phase': 'identification', 'category':
                                          'identity_mismatch' if state == 'mismatch' else 'identity_unavailable'}

    def command(self, phase, values, allow_nonzero=False):
        if phase not in PHASES:
            raise ValueError('invalid command phase')
        self.add({'kind': 'command', 'phase': phase, 'values': fields(values, COMMAND_FIELDS)})
        if values.get('output_bound') is True or values.get('truncated') is True:
            raise Failure(phase, 'output_bound')
        if values.get('exit') is None and values.get('signal') is None:
            raise Failure(phase, 'unknown')
        if not allow_nonzero and (values.get('exit') not in (None, 0) or values.get('signal') not in (None, 0)):
            raise Failure(phase, 'nonzero')

    def register_group(self, handle):
        self.groups.append(handle)
        if len(self.groups) > MAX_GROUPS:
            raise Failure('launch', 'output_bound')


def failure_record(error, phase):
    if isinstance(error, Failure):
        return {'phase': error.phase, 'category': error.category}
    if isinstance(error, TimeoutError):
        return {'phase': phase, 'category': 'timeout'}
    if isinstance(error, (KeyboardInterrupt, SystemExit)):
        return {'phase': phase, 'category': 'cancellation'}
    return {'phase': phase, 'category': 'unknown'}


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


def held_session(identity, get_identity, get_session):
    """Check a continuously held process around one exact-PID session query."""
    pid = identity.get('pid')
    keys = ('pid', 'ppid', 'uid', 'pgid', 'start_sec', 'start_usec')
    if not integer(pid) or pid <= 0 or any(key not in identity for key in keys):
        return None
    expected = {key: identity[key] for key in keys}
    try:
        if get_identity(pid) != expected:
            return None
        session = get_session(pid)
        if not integer(session) or get_identity(pid) != expected:
            return None
        return session
    except Exception:
        return None


def valid_registered_identity(expected):
    if not isinstance(expected, dict):
        return False
    limits = {'pid': 1, 'ppid': 0, 'uid': 0, 'pgid': 1, 'session': 1, 'start_sec': 0, 'start_usec': 0}
    if any(not integer(expected.get(key)) or expected[key] < minimum for key, minimum in limits.items()):
        return False
    return expected['start_usec'] < 1000000


def validate_inventory_identities(identities):
    if (len(identities) != 3 or not all(valid_registered_identity(item) for item in identities) or
            len({item['pid'] for item in identities}) != 3):
        raise Failure('identification', 'identity_unavailable')


def valid_owned_evidence(role, expected, row, start):
    if (role not in ('probe', 'leader', 'command') or not valid_registered_identity(expected) or
            not isinstance(row, dict) or not integer(start) or start < 0):
        return False
    limits = {'pid': 1, 'ppid': 0, 'uid': 0, 'pgid': 1}
    return all(integer(row.get(key)) and row[key] >= limits[key] for key in ('pid', 'ppid', 'uid', 'pgid'))


def compare_owned_row(role, expected, row, start, observation, get_identity, get_session):
    """Compare historical rows and distinguish held SID from completed command."""
    if not valid_owned_evidence(role, expected, row, start):
        raise Failure('identification', 'identity_unavailable')
    event = identity_comparison(expected, dict(row, start_sec=start), problem='deferred')
    event['role'] = 'own_' + role
    event['ps_session_metadata'] = row.get('ps_session_metadata')
    event['scope'] = 'historical_completed_not_current_ownership' if role == 'command' else 'held'
    fields = {item['field']: item for item in event['fields']}
    session = fields['session']
    if role == 'command':
        session['expected_source'] = 'getsid(pre_exec)'
    session['observed_source'] = 'getsid(target_pid)' if role != 'command' else 'not_applicable_completed'
    session['observed'] = None
    try:
        for name in ('pid', 'ppid', 'uid', 'pgid'):
            field = fields[name]
            field['state'] = 'match' if row.get(name) == expected.get(name) else 'mismatch'
            if field['state'] != 'match':
                raise Failure('identification', 'identity_mismatch')
        if role == 'command':
            session['state'] = 'not_applicable'
        else:
            actual = held_session(expected, get_identity, get_session)
            session['observed'] = actual
            session['state'] = 'unknown' if actual is None else 'match' if actual == expected.get('session') else 'mismatch'
            if session['state'] != 'match':
                raise Failure('identification', 'identity_unavailable' if actual is None else 'identity_mismatch')
        fields['start_sec']['state'] = 'match' if start == expected.get('start_sec') else 'mismatch'
        if fields['start_sec']['state'] != 'match':
            raise Failure('identification', 'identity_mismatch')
    finally:
        observation.add(event)


def inventory_parse_failure(fields, identities, observation, problem):
    """Record parse failure only for an own row with matching parsed PID and UID."""
    try:
        pid, uid = int(fields[0]), int(fields[4])
    except (IndexError, ValueError):
        return
    if not integer(pid) or not integer(uid):
        return
    match = next(((index, item) for index, item in enumerate(identities)
                  if item['pid'] == pid and item['uid'] == uid), None)
    if match is None:
        return
    role_index, expected = match
    observed, redacted = {}, {}
    for index, name in enumerate(('pid', 'ppid', 'pgid', 'uid')):
        index = (0, 1, 2, 4)[index]
        if index < len(fields):
            try:
                value = int(fields[index])
                if integer(value):
                    observed[name] = value
                else:
                    redacted[name] = ('out_of_range_integer', len(fields[index]))
            except ValueError:
                redacted[name] = ('unparseable_text', len(fields[index]))
    if len(fields) > 5:
        start = ' '.join(fields[5:10])
        pattern = (r'(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun) (?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) '
                   r'\d{1,2} \d{2}:\d{2}:\d{2} \d{4}')
        if re.fullmatch(pattern, start, flags=re.ASCII):
            observed['start_sec'] = start
        else:
            redacted['start_sec'] = ('unparseable_text', len(start))
    event = identity_comparison(expected, observed, problem)
    if problem == 'row_shape':
        event['reason'] = 'row_shape'
    for item in event['fields']:
        if item['field'] in redacted:
            item['observed_type'], item['observed_length'] = redacted[item['field']]
            if item['field'] == 'start_sec':
                item['observed_source'], item['observed_unit'] = 'ps_lstart_raw', 'text'
            else:
                item['observed_unit'] = 'text'
    event['role'] = ('own_leader', 'own_command', 'own_probe')[role_index]
    observation.add(event)


def missing_identity_comparisons(own, identities, observation):
    for name, expected in identities:
        if name not in own:
            event = identity_comparison(expected, {}, problem='missing_row')
            event['role'] = 'own_' + name
            observation.add(event)


def parse_inventory_row(line, identities, observation):
    """Apply the existing ps parser, recording own-row parse failures."""
    parts = line.split(None, 10)
    if len(parts) != 11:
        inventory_parse_failure(parts, identities, observation, 'row_shape')
        raise Failure('inventory', 'malformed_inventory')
    values = []
    for index, name in ((0, 'pid'), (1, 'ppid'), (2, 'pgid'), (4, 'uid')):
        try:
            values.append(int(parts[index]))
        except ValueError:
            inventory_parse_failure(parts, identities, observation, name)
            raise Failure('inventory', 'malformed_inventory') from None
    pid, ppid, pgid, uid = values
    if uid != 501:
        return None
    ps_session = parts[3] if re.fullmatch(r'[0-9a-fA-F]{1,32}', parts[3], flags=re.ASCII) else None
    row = {'pid': pid, 'uid': uid, 'ppid': ppid, 'pgid': pgid, 'session': None,
           'ps_session_metadata': ps_session,
           'start': ' '.join(parts[5:10])}
    try:
        start = int(time.mktime(time.strptime(row['start'], '%a %b %d %H:%M:%S %Y')))
    except (ValueError, OverflowError):
        inventory_parse_failure(parts, identities, observation, 'start_sec')
        raise Failure('inventory', 'malformed_inventory') from None
    return row, start


def import_nested_frame(frame, observation):
    if not isinstance(frame, dict) or not isinstance(frame.get('events'), list):
        raise Failure('inventory', 'malformed_inventory')
    for event in frame['events']:
        observation.add(event)
    diagnostic = frame.get('candidate_diagnostic')
    try:
        qualified = candidate_diagnostics.qualification(diagnostic)
    except TimeoutError:
        if frame.get('ok') is True:
            raise
        qualified = 'unavailable'
    if qualified != 'unavailable':
        observation.candidate_diagnostic = diagnostic
    elif diagnostic is not None:
        observation.candidate_diagnostic = {'kind': 'inventory_candidates', 'diagnostic': 'unavailable'}
    if frame.get('ok') is not True:
        failure, secondary = frame.get('failure'), frame.get('secondary', [])
        if not isinstance(failure, dict) or not isinstance(secondary, list):
            raise Failure('inventory', 'malformed_inventory')
        allowed = [detail for detail in secondary[:8] if detail in SECONDARY_DETAILS]
        raise Failure(failure.get('phase'), failure.get('category'), allowed)
    if not isinstance(frame.get('value'), dict):
        raise Failure('inventory', 'malformed_inventory')
    return frame['value']


def nested_outcome(operation, observation):
    """Retain the nested failure and observations without emitting raw exceptions."""
    try:
        value = operation(observation)
        if observation.qualification_failure is not None:
            return {'ok': False, 'value': None, 'failure': observation.qualification_failure}
        if not any(event.get('kind') == 'command' for event in observation.events):
            return {'ok': False, 'value': None, 'failure': {'phase': 'inventory', 'category': 'unknown'}}
        if not any(event.get('kind') == 'identity' and event.get('role') == 'own_leader'
                   and event.get('state') == 'success' for event in observation.events):
            return {'ok': False, 'value': None,
                    'failure': {'phase': 'held_identity', 'category': 'identity_unavailable'}}
        return {'ok': True, 'value': value, 'failure': None}
    except BaseException as error:
        return {'ok': False, 'value': None, 'failure': failure_record(error, 'inventory'),
                'secondary': [detail for detail in error.secondary
                              if detail in SECONDARY_DETAILS]
                if isinstance(error, Failure) else []}


def nested(operation, observation):
    result = nested_outcome(operation, observation)
    result['candidate_diagnostic'] = observation.candidate_diagnostic
    return result


def cleanup_record(cleanup, handle):
    try:
        result = cleanup(handle)
        required = {'requested', 'operation', 'leader_reap', 'group_settlement'}
        if (not isinstance(result, dict) or not required <= set(result) or set(result) - required - {'errors'} or
                (result['requested'] is not None and type(result['requested']) is not bool) or
                any(result[key] not in STATES for key in required - {'requested'})):
            raise ValueError('invalid cleanup result')
        errors = result.get('errors', [])
        if (not isinstance(errors, list) or len(errors) > 8 or
                any(error not in SECONDARY_DETAILS for error in errors)):
            raise ValueError('invalid secondary cleanup errors')
        return dict(result)
    except BaseException:
        return {'requested': None, 'operation': 'unknown', 'leader_reap': 'unknown',
                'group_settlement': 'unknown', 'error': 'cleanup_exception'}


def encode(record):
    """No truncation: replace oversized observations with an explicit failure."""
    original = dict(record)
    diagnostic = original.pop('candidate_diagnostic', None)
    body = json.dumps(original, sort_keys=True, separators=(',', ':')).encode('utf-8')
    if len(body) <= MAX_RECORD:
        try:
            qualified = candidate_diagnostics.qualification(diagnostic)
        except TimeoutError:
            if original.get('failure') is None:
                raise DiagnosticFinalizationTimeout('diagnostic finalization timed out') from None
            qualified = 'unavailable'
        enriched = dict(original, diagnostic_qualification=qualified)
        if qualified != 'missing':
            enriched['candidate_diagnostic'] = ({'kind': 'inventory_candidates', 'diagnostic': 'unavailable'}
                                                if qualified == 'unavailable' else diagnostic)
        proposed = json.dumps(enriched, sort_keys=True, separators=(',', ':')).encode('utf-8')
        if len(proposed) <= MAX_RECORD:
            return proposed
        proposed = json.dumps(dict(original, diagnostic_qualification='omitted'),
                              sort_keys=True, separators=(',', ':')).encode('utf-8')
        if len(proposed) <= MAX_RECORD:
            return proposed
        return body
    bounded = {'ok': False, 'failure': record.get('failure') or
               {'phase': 'preparation', 'category': 'output_bound'},
               'secondary': record.get('secondary', []), 'events': [], 'cleanup': record.get('cleanup', []),
               'record_truncated': True}
    return json.dumps(bounded, sort_keys=True, separators=(',', ':')).encode('utf-8')


def encode_finalization_timeout(record):
    """Serialize a fixed failure without repeating diagnostic qualification."""
    result = dict(record)
    result.pop('candidate_diagnostic', None)
    result['ok'] = False
    result['failure'] = result.get('failure') or {'phase': 'preparation', 'category': 'timeout'}
    result['diagnostic_qualification'] = 'unavailable'
    result['candidate_diagnostic'] = {'kind': 'inventory_candidates', 'diagnostic': 'unavailable'}
    body = json.dumps(result, sort_keys=True, separators=(',', ':')).encode('utf-8')
    if len(body) <= MAX_RECORD:
        return body
    result.pop('candidate_diagnostic')
    result.pop('diagnostic_qualification')
    body = json.dumps(result, sort_keys=True, separators=(',', ':')).encode('utf-8')
    if len(body) <= MAX_RECORD:
        return body
    bounded = {'ok': False, 'failure': result['failure'], 'secondary': result['secondary'],
               'events': [], 'cleanup': result['cleanup'], 'record_truncated': True}
    return json.dumps(bounded, sort_keys=True, separators=(',', ':')).encode('utf-8')


def run(spawn, operation, cleanup, emit, clock=time.monotonic):
    """Emit one bounded record, then raise on any operation or cleanup failure."""
    observed = Observation()
    started = clock()
    primary = None
    early_secondary = []
    result = {'ok': False, 'value': None}
    groups = []
    try:
        try:
            spawn(observed.register_group)
            result = nested(operation, observed)
            primary = result['failure']
            early_secondary = result.get('secondary', [])
        except BaseException as error:
            if primary is None:
                primary = failure_record(error, 'launch')
            if isinstance(error, Failure):
                early_secondary = error.secondary
    finally:
        for handle in observed.groups:
            groups.append({'requested': None, 'operation': 'unknown', 'leader_reap': 'unknown',
                           'group_settlement': 'unknown', 'error': 'cleanup_exception'})
            groups[-1] = cleanup_record(cleanup, handle)
    secondary = [{'phase': 'cleanup', 'category': 'unknown', 'group': None, 'detail': detail}
                 for detail in early_secondary if detail in SECONDARY_DETAILS]
    secondary += [{'phase': 'cleanup', 'category': 'unknown', 'group': index}
                 for index, item in enumerate(groups)
                 if item.get('errors') or item['requested'] is not True or item['operation'] != 'success' or item['leader_reap'] != 'success'
                 or item['group_settlement'] != 'success']
    if primary is None and not groups:
        secondary.append({'phase': 'cleanup', 'category': 'unknown', 'group': None})
    if primary is None and secondary:
        primary = {'phase': 'cleanup', 'category': 'unknown'}
    elapsed = max(0, int((clock() - started) * 1000))
    record = {'ok': primary is None, 'failure': primary, 'secondary': secondary,
              'events': observed.events, 'cleanup': groups, 'elapsed_ms': elapsed,
              'record_truncated': False, 'candidate_diagnostic': observed.candidate_diagnostic}
    try:
        body = encode(record)
    except DiagnosticFinalizationTimeout:
        body = encode_finalization_timeout(record)
    decoded = json.loads(body)
    try:
        emit(body)
    except BaseException as error:
        if decoded['ok']:
            decoded['ok'] = False
            decoded['failure'] = {'phase': 'preparation', 'category': 'unknown'}
        raise ProtocolFailure(decoded) from error
    if not decoded['ok']:
        raise ProtocolFailure(decoded)
    return result['value']
