"""Settle a private job without accepting unidentified process termination."""
from contextlib import ExitStack
from time import monotonic, sleep


class SettlementError(RuntimeError):
    pass


def _milliseconds(end, clock):
    # Millisecond truncation can conservatively reject the final fractional millisecond.
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


def _identify(api, job, pin, baseline, end, clock, stack, evidence):
    pids = tuple(api.members(job))
    if len(pids) != len(set(pids)) or len(pids) != baseline['active']:
        raise SettlementError('incomplete job enumeration')
    processes = []
    for pid in pids:
        _milliseconds(end, clock)
        process = stack.enter_context(api.open_process(pid))
        processes.append((pid, process))
    for pid, process in processes:
        _milliseconds(end, clock)
        if not api.in_job(process, job):
            raise SettlementError('process left job')
        identity = pin.verify_process(api, process, job)
        evidence['residuals'].append({'pid': pid, 'identity': identity, 'member': True,
                                      'action': 'none', 'signaled': False})
    return processes


def _intervene(api, processes, evidence, end, clock):
    for (_, process), item in zip(processes, evidence['residuals']):
        _milliseconds(end, clock)
        if api.wait(process, 0):
            item.update(action='already_exited', signaled=True)
            continue
        api.terminate(process, 1)
        item['action'] = 'terminated'
        item['signaled'] = api.wait(process, _milliseconds(end, clock))
        if not item['signaled'] or clock() >= end:
            raise SettlementError('residual did not exit before deadline')


def _cleanup(api, job, pin, end, clock, evidence):
    baseline = _account(api, job)
    evidence['baseline'] = baseline
    with ExitStack() as stack:
        processes = _identify(api, job, pin, baseline, end, clock, stack, evidence)
        _milliseconds(end, clock)
        before = _account(api, job)
        evidence['before_intervention'] = before
        if before['total'] != baseline['total']:
            raise SettlementError('new job association before intervention')
        _intervene(api, processes, evidence, end, clock)
    evidence['residual_handles_closed'] = True
    _milliseconds(end, clock)
    final = _account(api, job)
    evidence['final'] = final
    if final['active'] != 0 or final['total'] != baseline['total']:
        raise SettlementError('job did not settle or association changed')
    evidence['result'] = 'telemetry_cleanup' if processes else 'natural'


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


def settle(api, job, root, pin, deadline, *, grace_seconds=1.0, clock=monotonic, pause=sleep):
    """Return detailed success or failure evidence; emergency never succeeds.

    `deadline` is the absolute monotonic root deadline. The caller retains the
    locked `pin` and private job through receipt finalization.
    """
    evidence = {'result': 'failed', 'root': {'pid': root.pid, 'signaled': False,
                'exit': None, 'closed': False}, 'root_deadline': deadline,
                'grace': {}, 'baseline': None, 'before_intervention': None,
                'final': None, 'residuals': [], 'residual_handles_closed': False,
                'emergency': False, 'emergency_errors': [], 'error': None}
    cleanup_end = None
    try:
        if not 0 <= grace_seconds <= 1:
            raise SettlementError('invalid grace')
        _root_exit(api, root, deadline, clock, evidence)
        grace_end = min(deadline, evidence['root']['observed_at'] + grace_seconds)
        evidence['grace']['deadline'] = grace_end
        _grace(api, job, grace_end, clock, pause, evidence)
        _milliseconds(deadline, clock)
        evidence['cleanup_start'] = clock()
        cleanup_end = evidence['cleanup_start'] + 10.0
        evidence['cleanup_deadline'] = cleanup_end
        _cleanup(api, job, pin, cleanup_end, clock, evidence)
        evidence['settled_at'] = clock()
    except BaseException as exc:
        evidence['error'] = repr(exc)
        if cleanup_end is None:
            cleanup_end = clock() + 10.0
            evidence['cleanup_deadline'] = cleanup_end
        _emergency(api, job, root, cleanup_end, clock, pause, evidence)
    return evidence
