"""Disabled production boundary and exact role-pin preparation for v2.

This source proposal does not supply a compiler-launch entry point. A later
qualified integration must retain all v2 and reused v1 module bytes before
import, and prove endpoint isolation independently before enabling admission.
"""
from contextlib import contextmanager, ExitStack
from dataclasses import asdict
import json
import os
import sys
from time import monotonic

QUALIFIED = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-fixture-detached-20261114/dev/windows/proof/build_only'


def _checked_runtime():
    """Require the external retained-input bootstrap and its fixed v1 origins."""
    for name, module in tuple(sys.modules.items()):
        if name == 'build_only' or name.startswith('build_only.'):
            filename = '__init__.py' if name == 'build_only' else name.split('.')[-1] + '.py'
            expected = os.path.normcase(os.path.abspath(os.path.join(QUALIFIED, filename)))
            if os.path.normcase(os.path.abspath(getattr(module, '__file__', ''))) != expected:
                raise RuntimeError('preloaded qualified module origin mismatch')
    if os.path.normcase(os.path.abspath(sys.path[0])) != os.path.normcase(os.path.abspath(os.path.dirname(QUALIFIED))):
        raise RuntimeError('qualified proof path must be first')
    from build_only import identity, policy, receipt_file, spawn, supervisor, winapi
    modules = (identity, policy, receipt_file, spawn, supervisor, winapi)
    for module in modules:
        expected = os.path.normcase(os.path.abspath(os.path.join(QUALIFIED, module.__name__.split('.')[-1] + '.py')))
        if os.path.normcase(os.path.abspath(module.__file__)) != expected:
            raise RuntimeError('qualified module origin mismatch')
    return supervisor, winapi


def _run_owned(profile, environment, receipt_path):
    """Internal pipeline; callers must first pass an explicit admission gate.

    The external PowerShell bootstrap must hold the exact v1/v2 files, Python,
    and parents before this interpreter imports any project code. It must also
    use a fresh private bytecode-cache location and verify fixed source origin.
    """
    from .lifecycle import CleanupHandoff
    handoff = CleanupHandoff()
    try:
        return _run_owned_impl(profile, environment, receipt_path, handoff)
    except BaseException as error:
        error.cleanup_handoff = handoff
        raise


def _run_owned_impl(profile, environment, receipt_path, handoff):
    old, winapi = _checked_runtime()
    from .profile import validate_environment, validate_profile
    validate_profile(profile)
    validate_environment(profile, environment)
    api = winapi.WinAPI()
    if not api.in_any_job():
        raise RuntimeError('outer helper job required')
    state = {'reason': None, 'errors': [], 'outcome': None, 'outputs': []}
    start = []
    with old._receipt(receipt_path) as receipt:
        failure = None
        try:
            _run_job(api, profile, environment, handoff, state, start, old)
        except BaseException as exc:
            failure = exc
            state['reason'] = repr(exc)
            state['errors'].extend(handoff.cleanup_errors)
            state['errors'].extend(old._close_retained(handoff))
        owned = {'opened': api.opened, 'closed': api.closed, 'failed': api.close_failed}
        if failure is None and (owned['opened'] != owned['closed'] or owned['failed']):
            failure = RuntimeError('owned handles did not close')
            state['reason'] = repr(failure)
        if failure is None:
            try:
                record = assemble(profile, state['outcome'], start[0], state['outputs'], owned)
            except BaseException as exc:
                failure = exc
                state['reason'] = repr(exc)[:1024]
        if failure is not None:
            record = {'schema': 'build-only-failed-v2', 'result': 'failed',
                      'evidence': state, 'owned': owned}
        try:
            receipt.write(json.dumps(record, separators=(',', ':')).encode('utf-8'))
            receipt.flush()
        except BaseException as error:
            if failure is not None:
                raise error from failure
            raise
        if failure is not None:
            raise RuntimeError('v2 supervision failed') from failure
    return record


def _run_job(api, profile, environment, handoff, state, started, old):
    import msvcrt
    from build_only.policy import PRODUCTION_POLICIES, inspect_command_inputs
    from build_only.spawn import LaunchFailure, launch
    from .lifecycle import settle
    policy = profile.command_policy
    if PRODUCTION_POLICIES:
        raise RuntimeError('production registry must start empty')
    PRODUCTION_POLICIES[policy.identity] = policy
    try:
        command = inspect_command_inputs(policy, environment)
        with old._pin_stack(handoff) as stack:
            executable = stack.enter_context(old._tracked_pin(handoff, api, command.executable,
                                                               command.executable_sha256, command.executable_size))
            response = stack.enter_context(old._tracked_pin(handoff, api, command.response_path,
                                                             command.response_sha256))
            pins = stack.enter_context(retained_role_pins(api, profile, old._tracked_pin, handoff))
            protected = {executable.file_id, response.file_id, *(pin.file_id for pin in pins.values())}
            if len(protected) != 4:
                raise ValueError('input image aliases another pin')
            stdout = stack.enter_context(old._stream(api, command.stdout))
            stderr = stack.enter_context(old._stream(api, command.stderr))
            job = stack.enter_context(api.create_job())
            executable.assert_path_unchanged()
            response.assert_path_unchanged()
            started.append(monotonic())
            try:
                root = launch(api, job, command.executable, command.args, command.cwd,
                              msvcrt.get_osfhandle(stdout.fileno()), msvcrt.get_osfhandle(stderr.fileno()),
                              lambda process, owned_job: executable.verify_process(api, process, owned_job), environment)
            except LaunchFailure as error:
                state['errors'].extend(old._close_launch_failure(api, job, error))
                raise
            handoff.root = root
            outcome = settle(api, job, root, pins, started[0] + policy.deadline_ms / 1000,
                             handoff=handoff, grace_seconds=policy.grace_ms / 1000)
            state['outcome'] = outcome
            if outcome['result'] == 'failed':
                raise RuntimeError('private job settlement failed')
            executable.assert_path_unchanged()
            response.assert_path_unchanged()
            state['outputs'] = old._output_evidence(api, command.outputs, protected)
    finally:
        PRODUCTION_POLICIES.pop(policy.identity, None)


def assemble(profile, outcome, started, outputs, owned):
    """Bind successful settlement observations to the reviewed profile."""
    from .profile import profile_hash
    if outcome['result'] == 'failed' or outcome['emergency']:
        raise ValueError('cannot assemble success from failed settlement')
    roles = {role.role: asdict(role) for role in profile.residual_roles}
    residuals = []
    for item in outcome['residuals']:
        expected = roles[item['role']]
        observed = item['identity']
        observed_id = ':'.join(str(observed[key]) for key in ('volume', 'file_index_high', 'file_index_low'))
        if (observed['sha256'].lower() != expected['sha256'] or observed['size'] != expected['size']
                or observed_id != expected['file_id']):
            raise ValueError('residual identity differs from role pin')
        identity = {'creation_time': observed['creation_time'], 'process_path': observed['path'],
                    'retained_final_path': observed['retained_final_path'],
                    'sha256': observed['sha256'].lower(), 'size': observed['size'],
                    'file_id': observed_id, 'expected_version': expected['version']}
        residuals.append({'pid': item['pid'], 'role': item['role'], 'identity': identity,
                          'member': item['member'], 'action': 'terminate' if item['action'] == 'terminated' else item['action'],
                          'signaled': item['signaled'], 'closed': item['closed']})
    return _record(profile, outcome, started, outputs, owned, residuals, profile_hash(profile))


def _record(profile, outcome, started, outputs, owned, residuals, digest):
    policy = profile.command_policy
    command = asdict(policy.command)
    for key in ('args', 'outputs', 'env_names'):
        command[key] = list(command[key])
    root = outcome['root']
    ms = lambda value: int(value * 1000)
    counts = lambda value: {'total': value['total'], 'active': value['active']}
    return {'schema': 'build-only-v2', 'source': policy.source_sha256, 'profile': digest,
            'endpoint': profile.endpoint, 'env_digest': profile.env_digest, 'command': command,
            'root': {'pid': root['pid'], 'creation_time': root['creation_time'], 'exit': root['exit'],
                     'signaled': root['signaled'], 'observed_ms': ms(root['observed_at']), 'closed': root['closed']},
            'deadlines': {'start_ms': ms(started), 'root_deadline_ms': ms(started) + policy.deadline_ms,
                          'grace_deadline_ms': ms(outcome['grace']['deadline']),
                          'cleanup_start_ms': ms(outcome['cleanup_start']),
                          'cleanup_deadline_ms': ms(outcome['cleanup_start']) + 10000,
                          'settled_ms': ms(outcome['settled_at'])},
            'pins': [asdict(role) for role in profile.residual_roles],
            'baseline': counts(outcome['baseline']), 'before': counts(outcome['before_intervention']),
            'final': counts(outcome['final']), 'verified_all_ms': ms(outcome['verified_all_at']),
            'intervention_ms': ms(outcome['intervention_start']), 'residuals': residuals,
            'result': outcome['result'], 'owned': owned, 'emergency_cleanup': False,
            'outputs': outputs, 'finalization': {'exclusive': True, 'write_complete': True}, 'errors': []}


@contextmanager
def retained_role_pins(api, profile, tracked_pin, handoff):
    """Retain both reviewed images until the caller exits this boundary."""
    from .profile import validate_profile
    validate_profile(profile)
    pins = {}
    with ExitStack() as stack:
        for role in profile.residual_roles:
            pin = stack.enter_context(tracked_pin(handoff, api, role.path, role.sha256, role.size))
            if ':'.join(str(part) for part in pin.file_id) != role.file_id:
                raise ValueError('role file identity mismatch')
            pins[role.role] = pin
        if len({pin.file_id for pin in pins.values()}) != 2:
            raise ValueError('role images alias')
        for pin in pins.values():
            pin.assert_path_unchanged()
        yield pins
        for pin in pins.values():
            pin.assert_path_unchanged()


def production(profile, environment, receipt_path):
    _checked_runtime()
    from .profile import admit_production
    admit_production(profile)
    return _run_owned(profile, environment, receipt_path)
