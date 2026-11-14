"""Private build-only supervision under the unchanged qualified outer helper.

An independently pinned outer-helper receipt must prove all inherited
descendants settled; inner job membership alone does not identify that helper.
"""
from contextlib import ExitStack, contextmanager
from dataclasses import asdict
import hashlib
import json
import os
from pathlib import Path
import sys
from time import monotonic

from .identity import FilePin, ObservedFile, RetainedHandlesError
from .lifecycle import settle
from .policy import PRODUCTION_POLICIES, inspect_command_inputs, policy_hash
from .spawn import launch, LaunchFailure
from .winapi import WinAPI
from .receipt_file import ReceiptFile, ReceiptCloseError

SOURCE_FILES = ('__init__.py', 'identity.py', 'lifecycle.py', 'policy.py', 'receipt.py',
                'receipt_file.py', 'spawn.py', 'supervisor.py', 'winapi.py')


class UnsettledRootError(RuntimeError):
    def __init__(self, root):
        super().__init__('root unresolved; outer helper settlement required')
        self.root = root


def _hash(path):
    digest = hashlib.sha256()
    size = 0
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
            size += len(block)
    if size == 0:
        raise ValueError('empty input or output')
    return digest.hexdigest(), size


def _environment(command):
    return dict(os.environ)


def _open_receipt(path):
    return ReceiptFile(path)


@contextmanager
def _receipt(path):
    try:
        with _open_receipt(path) as receipt:
            yield receipt
    except ReceiptCloseError as exc:
        for handle in exc.retained_handles:
            try:
                handle.close()
            except BaseException:
                pass
        raise


def _pin_sources(api, stack, expected):
    directory = Path(os.path.abspath(__file__)).parent
    records = []
    pins = []
    for name in SOURCE_FILES:
        path = directory / name
        digest, size = _hash(path)
        pin = stack.enter_context(FilePin(api, str(path), digest, size))
        records.append([name, digest])
        pins.append(pin)
    encoded = json.dumps(records, separators=(',', ':'), ensure_ascii=True).encode('ascii')
    if hashlib.sha256(encoded).hexdigest() != expected:
        raise ValueError('package source identity mismatch')
    return pins


@contextmanager
def _stream(api, path):
    stream = open(path, 'xb')
    api.opened += 1
    try:
        yield stream
    finally:
        try:
            stream.close()
            api.closed += 1
        except BaseException:
            api.close_failed += 1
            raise


def _output_evidence(api, paths, protected_ids):
    result = []
    for path in paths:
        with ObservedFile(api, path) as output:
            if output.size == 0 or output.file_id in protected_ids:
                raise ValueError('empty or aliased output')
            protected_ids.add(output.file_id)
            result.append({'path': path, 'sha256': output.sha256.lower(), 'size': output.size,
                           'fresh_before_launch': True})
    return result


def _millis(value):
    return int(value * 1000)


def _counts(value):
    return {'total': value['total'], 'active': value['active']}


def _assemble(policy, state, api, start):
    outcome = state['outcome']
    root = outcome['root']
    pin = state['pin']
    residuals = []
    for item in outcome['residuals']:
        identity = item['identity']
        if (identity['sha256'].lower() != pin['sha256'] or identity['size'] != pin['size']):
            raise ValueError('residual pin mismatch')
        residuals.append({'pid': item['pid'], 'creation_time': identity['creation_time'], **pin, 'member': item['member'],
                          'action': 'terminate' if item['action'] == 'terminated' else item['action'],
                          'signaled': item['signaled'], 'closed': outcome['residual_handles_closed']})
    command = asdict(policy.command)
    for key in ('args', 'outputs', 'env_names'):
        command[key] = list(command[key])
    return {'schema': 'build-only-v1', 'source': policy.source_sha256, 'policy': policy_hash(policy),
            'policy_kind': policy.kind, 'result': outcome['result'], 'command': command,
            'root': {'pid': root['pid'], 'creation_time': root['creation_time'], 'exit': root['exit'],
                     'signaled': root['signaled'], 'observed_ms': _millis(root['observed_at']),
                     'closed': root['closed']},
            'deadlines': {'start_ms': _millis(start), 'root_deadline_ms': _millis(start) + policy.deadline_ms,
                          'grace_deadline_ms': _millis(outcome['grace']['deadline']),
                          'cleanup_start_ms': _millis(outcome['cleanup_start']),
                          'cleanup_deadline_ms': _millis(outcome['cleanup_start']) + 10000,
                          'settled_ms': _millis(outcome['settled_at'])},
            'file_pin': pin, 'residuals': residuals, 'baseline': _counts(outcome['baseline']),
            'before': _counts(outcome['before_intervention']), 'final': _counts(outcome['final']),
            'owned': {'opened': api.opened, 'closed': api.closed, 'failed': api.close_failed},
            'forced_cleanup': outcome['result'] == 'telemetry_cleanup',
            'emergency_cleanup': outcome['emergency'], 'outputs': state['outputs'],
            'finalization': {'exclusive': True, 'write_complete': True}, 'errors': state['errors']}


def _close_launch_failure(api, job, failure):
    errors = []
    deadline = failure.cleanup_deadline
    if deadline is None:
        return ['missing cleanup deadline; outer helper settlement required']
    if monotonic() >= deadline:
        return ['cleanup deadline exceeded; outer helper settlement required']
    try:
        api.terminate_job(job, 1)
    except BaseException as exc:
        errors.append(repr(exc))
    if failure.root is not None:
        root_signaled = False
        try:
            if monotonic() >= deadline:
                raise TimeoutError('cleanup deadline exceeded')
            if not api.wait(failure.root.process, 0):
                api.terminate(failure.root.process, 1)
            remaining = deadline - monotonic()
            if remaining <= 0:
                raise TimeoutError('cleanup deadline exceeded')
            root_signaled = failure.root.wait(max(1, int(remaining * 1000)))
            if not root_signaled:
                errors.append('root did not signal')
        except BaseException as exc:
            errors.append(repr(exc))
        if root_signaled:
            try:
                failure.root.close()
            except BaseException as exc:
                errors.append(repr(exc))
        else:
            errors.append('root handle unresolved; outer helper settlement required')
    for handle in failure.retained_handles:
        try:
            handle.close()
        except BaseException as exc:
            errors.append(repr(exc))
    try:
        while monotonic() < deadline:
            if api.accounting(job)[0] == 0:
                break
            from time import sleep
            sleep(min(0.025, max(0, deadline - monotonic())))
        if api.accounting(job)[0] != 0:
            errors.append('job did not settle')
    except BaseException as exc:
        errors.append(repr(exc))
    return errors


def _run_job(api, policy, env, started, receipt):
    import msvcrt
    command = inspect_command_inputs(policy, env)
    with ExitStack() as stack:
        sources = _pin_sources(api, stack, policy.source_sha256)
        executable = stack.enter_context(FilePin(api, command.executable, command.executable_sha256,
                                                 command.executable_size))
        response = stack.enter_context(FilePin(api, command.response_path, command.response_sha256))
        telemetry = stack.enter_context(FilePin(api, policy.telemetry_path, policy.telemetry_sha256,
                                                policy.telemetry_size))
        observed_id = ':'.join(str(part) for part in telemetry.file_id)
        if observed_id != policy.telemetry_file_id:
            raise ValueError('telemetry file identity mismatch')
        if policy.kind == 'fixture' and (telemetry.file_id != executable.file_id or
                telemetry.final_path != executable.final_path or telemetry.sha256 != executable.sha256 or
                telemetry.size != executable.size):
            raise ValueError('fixture telemetry is not pinned Python')
        protected_ids = {item.file_id for item in (*sources, executable, response, telemetry)}
        # Version is policy metadata bound to these pinned bytes, not an observed resource.
        receipt['pin'] = {'path': policy.telemetry_path, 'sha256': telemetry.sha256.lower(),
                          'size': telemetry.size, 'version': policy.telemetry_version,
                          'file_id': observed_id}
        stdout = stack.enter_context(_stream(api, command.stdout))
        stderr = stack.enter_context(_stream(api, command.stderr))
        job = stack.enter_context(api.create_job())
        executable.assert_path_unchanged()
        response.assert_path_unchanged()
        telemetry.assert_path_unchanged()
        for source in sources:
            source.assert_path_unchanged()
        started.append(monotonic())
        try:
            root = launch(api, job, command.executable, command.args, command.cwd,
                          msvcrt.get_osfhandle(stdout.fileno()), msvcrt.get_osfhandle(stderr.fileno()),
                          lambda process, owned_job: executable.verify_process(api, process, owned_job), env)
        except LaunchFailure as exc:
            receipt['errors'].extend(_close_launch_failure(api, job, exc))
            raise
        try:
            outcome = settle(api, job, root, telemetry, started[0] + policy.deadline_ms / 1000,
                             grace_seconds=policy.grace_ms / 1000)
        except BaseException:
            failure = LaunchFailure('interrupted', root, (), True, cleanup_deadline=monotonic() + 10.0)
            receipt['errors'].extend(_close_launch_failure(api, job, failure))
            raise
        receipt['outcome'] = outcome
        if outcome['result'] == 'failed':
            if outcome['root'].get('unresolved'):
                raise UnsettledRootError(root)
            raise RuntimeError('private job settlement failed')
        executable.assert_path_unchanged()
        response.assert_path_unchanged()
        telemetry.assert_path_unchanged()
        for source in sources:
            source.assert_path_unchanged()
        receipt['outputs'] = _output_evidence(api, command.outputs, protected_ids)
    if api.opened != api.closed or api.close_failed:
        raise RuntimeError('owned handles did not close')


def _supervise(policy, receipt_path):
    env = _environment(policy.command)
    inspect_command_inputs(policy, env)
    api = WinAPI()
    if not api.in_any_job():
        raise RuntimeError('qualified outer helper job is required')
    state = {'reason': None, 'errors': [], 'outcome': None, 'outputs': [], 'owned': None}
    started = []
    with _receipt(receipt_path) as receipt:
        failure = None
        try:
            _run_job(api, policy, env, started, state)
        except RetainedHandlesError as exc:
            state['reason'] = repr(exc)
            for handle in exc.retained_handles:
                try:
                    handle.close()
                except BaseException as close_error:
                    state['errors'].append(repr(close_error))
            failure = exc
        except BaseException as exc:
            state['reason'] = repr(exc)
            failure = exc
        state['owned'] = {'opened': api.opened, 'closed': api.closed, 'failed': api.close_failed}
        try:
            record = (_assemble(policy, state, api, started[0]) if failure is None else
                      {'schema': 'build-only-failed-v1', 'policy': policy_hash(policy),
                       'result': 'failed', 'evidence': state})
        except BaseException as exc:
            failure = exc
            state['reason'] = repr(exc)
            record = {'schema': 'build-only-failed-v1', 'policy': policy_hash(policy),
                      'result': 'failed', 'evidence': state}
        receipt.write(json.dumps(record, separators=(',', ':')).encode('utf-8'))
        receipt.flush()
    if failure is not None:
        raise RuntimeError('build-only supervision failed') from failure
    return record


def production(policy_name, receipt_path):
    policy = PRODUCTION_POLICIES.get(policy_name)
    if policy is None or policy.kind != 'production':
        raise ValueError('unregistered production policy')
    return _supervise(policy, receipt_path)


def _validate_fixture(policy):
    if policy.kind != 'fixture' or policy.command_form != 'fixture':
        raise ValueError('fixture policy required')
    if policy.deadline_ms != 4000 or policy.grace_ms != 100 or policy.telemetry_version != 'fixture-python':
        raise ValueError('fixture timing or telemetry metadata mismatch')
    directory = Path(__file__).resolve(strict=True).parent
    project_deps = directory.parents[3] / 'deps'
    command = policy.command
    target = Path(command.cwd)
    if not target.is_absolute():
        raise ValueError('absolute fixture directory required')
    target = target.resolve(strict=True)
    if not target.is_dir() or target == project_deps or not target.is_relative_to(project_deps):
        raise ValueError('fixture directory must be under project deps')
    executable = str(Path(sys.executable).resolve(strict=True))
    child = str((directory / 'fixture_child.py').resolve(strict=True))
    output = str(target / 'result.bin')
    modes = ('natural', 'residual', 'transientchild', 'spawnlate', 'rootfail', 'timeout')
    if (policy.telemetry_path != executable or policy.telemetry_sha256 != command.executable_sha256 or
            policy.telemetry_size != command.executable_size or command.executable != executable or command.response_path != child or
            len(command.args) != 4 or command.args[:2] != ('-B', child) or
            command.args[2] not in modes or command.args[3] != output or
            command.outputs != (output,) or command.cwd != str(target) or
            command.stdout != str(target / 'stdout.log') or command.stderr != str(target / 'stderr.log')):
        raise ValueError('fixture command differs from reviewed fixture')


def fixture(policy, receipt_path):
    _validate_fixture(policy)
    return _supervise(policy, receipt_path)
