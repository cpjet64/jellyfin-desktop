"""Bounded, transport-neutral failure records for supervised settlement checks.

This module performs no process, filesystem, network, or guest operations.
The caller supplies existing operations and a protected response-channel emitter.
"""
import json
import time

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


def nested(operation, observation):
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
                'secondary': error.secondary if isinstance(error, Failure) else []}


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
                any(error not in ('fd_close_failed', 'mask_restore_failed') for error in errors)):
            raise ValueError('invalid secondary cleanup errors')
        return dict(result)
    except BaseException:
        return {'requested': None, 'operation': 'unknown', 'leader_reap': 'unknown',
                'group_settlement': 'unknown', 'error': 'cleanup_exception'}


def encode(record):
    """No truncation: replace oversized observations with an explicit failure."""
    body = json.dumps(record, sort_keys=True, separators=(',', ':')).encode('utf-8')
    if len(body) <= MAX_RECORD:
        return body
    bounded = {'ok': False, 'failure': record.get('failure') or
               {'phase': 'preparation', 'category': 'output_bound'},
               'secondary': record.get('secondary', []), 'events': [], 'cleanup': record.get('cleanup', []),
               'record_truncated': True}
    return json.dumps(bounded, sort_keys=True, separators=(',', ':')).encode('utf-8')


def run(spawn, operation, cleanup, emit, clock=time.monotonic):
    """Emit one bounded record, then raise on any operation or cleanup failure."""
    observed = Observation()
    started = clock()
    primary = None
    early_secondary = []
    try:
        spawn(observed.register_group)
        result = nested(operation, observed)
        primary = result['failure']
        early_secondary = result.get('secondary', [])
    except BaseException as error:
        result = {'ok': False, 'value': None}
        primary = failure_record(error, 'launch')
        if isinstance(error, Failure):
            early_secondary = error.secondary
    groups = [cleanup_record(cleanup, handle) for handle in observed.groups]
    secondary = [{'phase': 'cleanup', 'category': 'unknown', 'group': None, 'detail': detail}
                 for detail in early_secondary if detail in ('fd_close_failed', 'mask_restore_failed')]
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
              'record_truncated': False}
    body = encode(record)
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
