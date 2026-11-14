"""Settle a private job without accepting unidentified process termination."""
from contextlib import ExitStack
import ntpath
from time import monotonic, sleep
from build_only.identity import RetainedHandlesError


class SettlementError(RuntimeError):
    pass


class CleanupHandoff:
    """Private owner for exact unresolved objects, never serialized in receipts."""
    def __init__(self):
        self.handles = []
        self.attempted = set()
        self.deadline = None
        self.root = None
        self.original_error = None
        self.primary_error = None
        self.cleanup_errors = []

    def retain(self, error):
        if self.original_error is None:
            self.original_error = error
        for handle in error.retained_handles:
            if not any(handle is existing for existing in self.handles):
                self.handles.append(handle)


def _milliseconds(end, clock):
    remaining = end - clock()
    if remaining <= 0:
        raise SettlementError('deadline exceeded')
    return max(1, int(remaining * 1000))


def _account(api, job):
    active, total = api.accounting(job)
    return {'active': active, 'total': total}


def _root_exit(api, root, deadline, clock, evidence):
    evidence['root']['creation_time'] = api.creation_time(root.process)
    evidence['root']['signaled'] = root.wait(_milliseconds(deadline, clock))
    evidence['root']['observed_at'] = clock()
    if not evidence['root']['signaled'] or evidence['root']['observed_at'] >= deadline:
        raise SettlementError('root deadline exceeded')
    evidence['root']['exit'] = root.exit_code()
    if clock() >= deadline or evidence['root']['exit'] != 0:
        raise SettlementError('root failed or exceeded deadline')
    root.close()
    evidence['root']['closed'] = True


def _grace(api, job, end, clock, pause, evidence):
    while True:
        state = _account(api, job)
        evidence['grace']['last'] = state
        if state['active'] == 0 or clock() >= end:
            return state
        pause(min(0.025, max(0.0, end - clock())))


def _identify(api, job, pins, baseline, end, clock, stack, evidence, handoff):
    pids = tuple(api.members(job))
    if len(pids) != len(set(pids)) or len(pids) != baseline['active']:
        raise SettlementError('incomplete job enumeration')
    processes = []
    for pid in pids:
        _milliseconds(end, clock)
        process = stack.enter_context(api.open_process(pid))
        processes.append((pid, process))
    used = set()
    for pid, process in processes:
        _milliseconds(end, clock)
        if not api.in_job(process, job):
            raise SettlementError('process left job')
        try:
            image = ntpath.normcase(ntpath.normpath(api.image_path(process)))
            matches = [(role, pin) for role, pin in pins.items()
                       if ntpath.normcase(ntpath.normpath(pin.path)) == image]
            if len(matches) != 1 or matches[0][0] in used:
                raise SettlementError('unknown, ambiguous, or excessive residual')
            role, pin = matches[0]
            identity = pin.verify_process(api, process, job)
            used.add(role)
        except Exception as exc:
            if isinstance(exc, RetainedHandlesError):
                handoff.retain(exc)
            observation = getattr(exc, 'identity_observation', None)
            cause = exc.__cause__
            retained = len(getattr(exc, 'retained_handles', ()))
            cause_type = type(cause).__name__ if cause is not None else None
            raise SettlementError(
                f'identity verification failed: pid={pid}, operation=verify_process, '
                f'exception={type(exc).__name__}, detail={exc!r}, observation={observation!r}, '
                f'retained_handle_count={retained}, cause_type={cause_type}, cause={cause!r}'
            ) from exc
        evidence['residuals'].append({'pid': pid, 'role': role, 'identity': identity, 'member': True,
                                      'action': 'none', 'signaled': False, 'closed': False})
    return processes


def _intervene(api, job, processes, evidence, end, clock):
    for (_, process), item in zip(processes, evidence['residuals']):
        _milliseconds(end, clock)
        if _account(api, job)['total'] != evidence['baseline']['total']:
            raise SettlementError('new job association during intervention')
        if api.wait(process, 0):
            item.update(action='already_exited', signaled=True)
            continue
        if not api.in_job(process, job) or api.creation_time(process) != item['identity']['creation_time']:
            raise SettlementError('residual identity or membership changed')
        api.terminate(process, 1)
        item['action'] = 'terminated'
        item['signaled'] = api.wait(process, _milliseconds(end, clock))
        if not item['signaled'] or clock() >= end:
            raise SettlementError('residual did not exit before deadline')


def _cleanup(api, job, pins, end, clock, evidence, handoff):
    baseline = _account(api, job)
    evidence['baseline'] = baseline
    with ExitStack() as stack:
        processes = _identify(api, job, pins, baseline, end, clock, stack, evidence, handoff)
        evidence['verified_all_at'] = clock()
        _milliseconds(end, clock)
        before = _account(api, job)
        evidence['before_intervention'] = before
        if before['total'] != baseline['total']:
            raise SettlementError('new job association before intervention')
        evidence['intervention_start'] = clock()
        _intervene(api, job, processes, evidence, end, clock)
    evidence['residual_handles_closed'] = True
    for item in evidence['residuals']:
        item['closed'] = True
    _milliseconds(end, clock)
    final = _account(api, job)
    evidence['final'] = final
    observed = clock()
    if observed >= end:
        raise SettlementError('cleanup deadline exceeded after final accounting')
    if final['active'] != 0 or final['total'] != baseline['total']:
        raise SettlementError('job did not settle or association changed')
    evidence['settled_at'] = observed
    roles = {item['role'] for item in evidence['residuals']}
    evidence['result'] = ('build_service_cleanup' if 'mspdbsrv' in roles else
                          'telemetry_cleanup' if processes else 'natural')


def _emergency(api, job, root, end, clock, pause, evidence):
    evidence['emergency'] = True
    errors = evidence['emergency_errors']
    try:
        api.terminate_job(job, 1)
    except BaseException as exc:
        errors.append('terminate_job: ' + repr(exc))
    try:
        if not evidence['root']['closed']:
            evidence['root']['emergency_signaled'] = root.wait(_milliseconds(end, clock))
    except BaseException as exc:
        errors.append('root_wait: ' + repr(exc))
    try:
        if not evidence['root']['closed'] and evidence['root'].get('emergency_signaled') is True:
            root.close()
            evidence['root']['closed'] = True
    except BaseException as exc:
        errors.append('root_close: ' + repr(exc))
    if not evidence['root']['closed']:
        evidence['root']['unresolved'] = True
        evidence['root']['outer_settlement_required'] = True
    try:
        while True:
            evidence['emergency_final'] = _account(api, job)
            if evidence['emergency_final']['active'] == 0 or clock() >= end:
                break
            pause(min(0.025, max(0.0, end - clock())))
    except BaseException as exc:
        errors.append('accounting: ' + repr(exc))


def settle(api, job, root, pins, deadline, *, handoff, grace_seconds=1.0, clock=monotonic, pause=sleep):
    """Return detailed success or failure evidence; emergency never succeeds.

    `deadline` is the absolute monotonic root deadline. The caller retains the
    locked role pins and private job through receipt finalization.
    """
    evidence = {'result': 'failed', 'root': {'pid': root.pid, 'signaled': False,
                'exit': None, 'closed': False}, 'root_deadline': deadline,
                'grace': {}, 'baseline': None, 'before_intervention': None,
                'final': None, 'residuals': [], 'residual_handles_closed': False,
                'emergency': False, 'emergency_errors': [], 'error': None}
    handoff.root = root
    cleanup_end = None
    try:
        if not 0 <= grace_seconds <= 1:
            raise SettlementError('invalid grace')
        if set(pins) != {'vctip', 'mspdbsrv'} or len({ntpath.normcase(ntpath.normpath(p.path)) for p in pins.values()}) != 2:
            raise SettlementError('invalid role pins')
        _root_exit(api, root, deadline, clock, evidence)
        grace_end = min(deadline, evidence['root']['observed_at'] + grace_seconds)
        evidence['grace']['deadline'] = grace_end
        _grace(api, job, grace_end, clock, pause, evidence)
        _milliseconds(deadline, clock)
        evidence['cleanup_start'] = clock()
        cleanup_end = evidence['cleanup_start'] + 10.0
        evidence['cleanup_deadline'] = cleanup_end
        handoff.deadline = cleanup_end
        _cleanup(api, job, pins, cleanup_end, clock, evidence, handoff)
    except BaseException as exc:
        evidence['result'] = 'failed'
        evidence['error'] = repr(exc)
        if cleanup_end is None:
            cleanup_end = clock() + 10.0
            evidence['cleanup_deadline'] = cleanup_end
            handoff.deadline = cleanup_end
        _emergency(api, job, root, cleanup_end, clock, pause, evidence)
    if evidence['root']['closed']:
        handoff.root = None
    return evidence
