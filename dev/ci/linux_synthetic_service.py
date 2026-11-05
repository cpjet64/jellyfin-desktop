"""Offline synthetic-service orchestration against an injected manager only.

No systemd, subprocess, filesystem, or signal implementation is provided here.
"""
import re
import time

MODES = ('ordinary', 'stopped-child', 'term-resistant', 'session-changing')
TOTAL_SECONDS = 60
START_SECONDS = 12
RUN_SECONDS = 25
STOP_SECONDS = 15
MANAGER_START_SECONDS = 10
MANAGER_STOP_SECONDS = 8
MANAGER_RUNTIME_SECONDS = 24


class Unconfirmed(RuntimeError):
    """The manager or process outcome cannot be established safely."""


class JobReplyUnconfirmed(Unconfirmed):
    def __init__(self, job):
        super().__init__('job reply received but connection continuity unconfirmed')
        self.job = job


class Rejected(RuntimeError):
    """The manager definitively rejected creation without a job."""


class FixtureFailed(RuntimeError):
    """The unit settled but the synthetic fixture did not qualify."""


def remaining(clock, deadline):
    value = deadline - clock()
    if value <= 0:
        raise Unconfirmed('deadline expired; manager outcome unconfirmed')
    return value


def synthetic_spec(task, mode):
    if not re.fullmatch('[0-9a-f]{32}', task) or mode not in MODES:
        raise ValueError('invalid synthetic task or mode')
    workspace = '/home/gitlab-runner/w644-' + task
    name = 'w644-' + task + '.service'
    return {'unit': name, 'workspace': workspace, 'executable': '/usr/bin/python3',
            'arguments': ('-I', '-S', '-B', workspace + '/synthetic_fixture.py', mode),
            'environment': {'HOME': workspace, 'LANG': 'C', 'PATH': '/usr/bin:/bin'},
            'properties': {'Type': 'exec', 'User': '999', 'Group': '988', 'Restart': 'no',
                           'Delegate': False, 'KillMode': 'control-group', 'SendSIGKILL': True,
                           'RuntimeMaxSec': MANAGER_RUNTIME_SECONDS, 'TimeoutStartSec': MANAGER_START_SECONDS,
                           'TimeoutStopSec': MANAGER_STOP_SECONDS},
            'manager': {'bus': 'system', 'add_ref': True, 'watch_jobs_before_start': True},
            'evidence': workspace + '/evidence.json'}


def bind(spec, job, observed):
    if (not isinstance(job, str) or not re.fullmatch('/org/freedesktop/systemd1/job/[1-9][0-9]*', job) or
            not isinstance(observed, dict) or observed.get('unit') != spec['unit'] or
            observed.get('job') != job or not re.fullmatch('[0-9a-f]{32}', str(observed.get('invocation', ''))) or
            observed.get('cgroup') != '/system.slice/' + spec['unit'] or observed.get('reference_held') is not True or
            observed.get('bus') != 'system' or observed.get('job_observed') is not True):
        raise Unconfirmed('unit, job, invocation, cgroup or reference identity unconfirmed')
    return {'unit': spec['unit'], 'job': job, 'invocation': observed['invocation'], 'cgroup': observed['cgroup']}


def verified_snapshot(identity, value):
    if not isinstance(value, dict) or any(value.get(k) != v for k, v in identity.items()):
        raise Unconfirmed('manager observation missing or identity changed')
    if value.get('reference_held') is not True:
        raise Unconfirmed('unit reference lost before evidence capture')
    return value


def capture(backend, identity, deadline, clock, cursor=None):
    remaining(clock, deadline)
    try:
        value, next_cursor = (backend.capture(identity, deadline) if cursor is None else
                              backend.next_change(identity, cursor, deadline))
    except BaseException as error:
        raise Unconfirmed('manager evidence unavailable') from error
    remaining(clock, deadline)
    if type(next_cursor) is not int or next_cursor < 0 or (cursor is not None and next_cursor <= cursor):
        raise Unconfirmed('watch cursor missing or did not advance')
    return verified_snapshot(identity, value), next_cursor


def require_settled(snapshot):
    if (snapshot.get('state') not in ('inactive', 'failed') or snapshot.get('job_terminal') is not True or
            snapshot.get('cgroup_empty') is not True or snapshot.get('descendants_absent') is not True or
            snapshot.get('evidence_saved') is not True):
        raise Unconfirmed('settlement or pre-collection evidence unconfirmed')
    if snapshot.get('state') == 'failed' or snapshot.get('fixture_result') != 'passed':
        raise FixtureFailed('synthetic fixture did not qualify')


def wait_for_fixture(backend, identity, mode, deadline, clock):
    """Use an installed-watch cursor so capture-to-wait changes are retained."""
    cursor = None
    while True:
        snapshot, cursor = capture(backend, identity, deadline, clock, cursor)
        if snapshot.get('evidence_saved') is not True:
            raise Unconfirmed('fixture evidence not saved')
        state = snapshot.get('state')
        if state in ('inactive', 'failed'):
            return snapshot
        if state != 'active':
            raise Unconfirmed('fixture state unconfirmed')
        if mode != 'ordinary' and snapshot.get('fixture_ready') is True:
            if snapshot.get('fixture_mode') != mode:
                raise Unconfirmed('fixture readiness mode mismatch')
            return snapshot


def verify_stop(identity, stop_job, value):
    if (not isinstance(value, dict) or any(value.get(key) != identity[key] for key in ('unit', 'invocation', 'cgroup')) or
            not re.fullmatch('/org/freedesktop/systemd1/job/[1-9][0-9]*', str(value.get('stop_job', ''))) or
            value.get('stop_job') != stop_job or stop_job == identity['job'] or value.get('observed_job') != stop_job or
            value.get('observed_unit') != identity['unit'] or value.get('job_observed') is not True or
            value.get('job_terminal') is not True or value.get('result') != 'done'):
        raise Unconfirmed('stop job or identity not confirmed')


def finish(backend, identity, snapshot, deadline, clock, premature=False):
    failure = None
    try:
        require_settled(snapshot)
        if premature:
            failure = FixtureFailed('fixture completed before required readiness')
    except FixtureFailed as error:
        failure = error
    remaining(clock, deadline)
    try:
        backend.release(identity, snapshot, deadline)
    except BaseException as error:
        raise Unconfirmed('reference release unconfirmed') from error
    remaining(clock, deadline)
    if failure:
        raise failure
    return snapshot


class SyntheticService:
    """One attempted manager submission; uncertainty permanently closes it."""
    def __init__(self, backend, clock=time.monotonic):
        self.backend = backend
        self.clock = clock
        self.attempted = False
        self.last_known = None

    def run(self, task, mode, stop_requested):
        if self.attempted:
            raise Unconfirmed('attempt already consumed; no resubmission')
        spec = synthetic_spec(task, mode)
        self.attempted = True
        self.last_known = {'task': task, 'unit': spec['unit'], 'submitted_job': None, 'binding': None,
                           'stop_attempted': False, 'stop_job': None}
        try:
            return self._run(spec, mode, stop_requested)
        except (Unconfirmed, Rejected, FixtureFailed) as error:
            error.last_known = dict(self.last_known)
            raise

    def _run(self, spec, mode, stop_requested):
        outer = self.clock() + TOTAL_SECONDS
        start = min(outer, self.clock() + START_SECONDS)
        remaining(self.clock, start)
        try:
            job = self.backend.submit(spec, start)
        except Rejected:
            raise
        except JobReplyUnconfirmed as error:
            if isinstance(error.job, str) and re.fullmatch('/org/freedesktop/systemd1/job/[1-9][0-9]*', error.job):
                self.last_known['submitted_job'] = error.job
            raise
        except BaseException as error:
            raise Unconfirmed('submission uncertain; do not resubmit or signal') from error
        if isinstance(job, str) and re.fullmatch('/org/freedesktop/systemd1/job/[1-9][0-9]*', job):
            self.last_known['submitted_job'] = job
        remaining(self.clock, start)
        try:
            observed = self.backend.observe_start(spec['unit'], job, start)
        except BaseException as error:
            raise Unconfirmed('start observation unavailable; do not signal') from error
        remaining(self.clock, start)
        identity = bind(spec, job, observed)
        self.last_known['binding'] = dict(identity)
        if observed.get('job_terminal') is not True or observed.get('start_result') != 'done':
            snapshot, _ = capture(self.backend, identity, start, self.clock)
            if snapshot.get('evidence_saved') is not True:
                raise Unconfirmed('start failure evidence not saved')
            raise Unconfirmed('start job or executable outcome unconfirmed')
        runtime = min(outer, self.clock() + RUN_SECONDS)
        snapshot = wait_for_fixture(self.backend, identity, mode, runtime, self.clock)
        if snapshot.get('state') in ('inactive', 'failed'):
            return finish(self.backend, identity, snapshot, runtime, self.clock, premature=mode != 'ordinary')
        if stop_requested is not True:
            raise Unconfirmed('synthetic stop not authorized')
        stop = min(outer, self.clock() + STOP_SECONDS)
        remaining(self.clock, stop)
        self.last_known['stop_attempted'] = True
        try:
            stop_job = self.backend.submit_stop(identity, stop)
        except JobReplyUnconfirmed as error:
            if isinstance(error.job, str) and re.fullmatch('/org/freedesktop/systemd1/job/[1-9][0-9]*', error.job):
                self.last_known['stop_job'] = error.job
            raise
        except BaseException as error:
            raise Unconfirmed('stop outcome uncertain; never retry automatically') from error
        if isinstance(stop_job, str) and re.fullmatch('/org/freedesktop/systemd1/job/[1-9][0-9]*', stop_job):
            self.last_known['stop_job'] = stop_job
        remaining(self.clock, stop)
        try:
            response = self.backend.observe_stop(identity, stop_job, stop)
        except BaseException as error:
            raise Unconfirmed('stop job observation unavailable; never retry automatically') from error
        remaining(self.clock, stop)
        try:
            verify_stop(identity, stop_job, response)
        except Unconfirmed as error:
            snapshot, _ = capture(self.backend, identity, stop, self.clock)
            if snapshot.get('evidence_saved') is not True:
                raise Unconfirmed('stop failure evidence not saved') from error
            raise
        snapshot, _ = capture(self.backend, identity, stop, self.clock)
        return finish(self.backend, identity, snapshot, stop, self.clock)
