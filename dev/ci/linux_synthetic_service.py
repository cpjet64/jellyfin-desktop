"""Offline synthetic-service orchestration against an injected manager only.

No systemd, subprocess, filesystem, or signal implementation is provided here.
"""
import re
import time

MODES = ('ordinary', 'stopped-child', 'term-resistant', 'session-changing')
TOTAL_SECONDS = 60
START_SECONDS = 15
RUN_SECONDS = 25
STOP_SECONDS = 15


class Unconfirmed(RuntimeError):
    """The manager or process outcome cannot be established safely."""


class Rejected(RuntimeError):
    """The manager definitively rejected creation without a job."""


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
                           'RuntimeMaxSec': RUN_SECONDS, 'TimeoutStartSec': START_SECONDS,
                           'TimeoutStopSec': STOP_SECONDS},
            'evidence': workspace + '/evidence.json'}


def bind(spec, job, observed):
    if (not isinstance(job, str) or not re.fullmatch('/org/freedesktop/systemd1/job/[1-9][0-9]*', job) or
            not isinstance(observed, dict) or observed.get('unit') != spec['unit'] or
            observed.get('job') != job or not re.fullmatch('[0-9a-f]{32}', str(observed.get('invocation', ''))) or
            observed.get('cgroup') != '/system.slice/' + spec['unit'] or observed.get('reference_held') is not True):
        raise Unconfirmed('unit, job, invocation, cgroup or reference identity unconfirmed')
    return {'unit': spec['unit'], 'job': job, 'invocation': observed['invocation'], 'cgroup': observed['cgroup']}


def verified_snapshot(identity, value):
    if not isinstance(value, dict) or any(value.get(k) != v for k, v in identity.items()):
        raise Unconfirmed('manager observation missing or identity changed')
    if value.get('reference_held') is not True:
        raise Unconfirmed('unit reference lost before evidence capture')
    return value


def capture(backend, identity, deadline, clock):
    remaining(clock, deadline)
    try:
        value = backend.capture(identity, deadline)
    except BaseException as error:
        raise Unconfirmed('manager evidence unavailable') from error
    remaining(clock, deadline)
    return verified_snapshot(identity, value)


def require_settled(snapshot):
    if (snapshot.get('state') not in ('inactive', 'failed') or snapshot.get('job_terminal') is not True or
            snapshot.get('cgroup_empty') is not True or snapshot.get('descendants_absent') is not True or
            snapshot.get('evidence_saved') is not True):
        raise Unconfirmed('settlement or pre-collection evidence unconfirmed')


def verify_stop(identity, value):
    if (not isinstance(value, dict) or any(value.get(key) != identity[key] for key in ('unit', 'invocation', 'cgroup')) or
            not re.fullmatch('/org/freedesktop/systemd1/job/[1-9][0-9]*', str(value.get('stop_job', ''))) or
            value.get('job_terminal') is not True or value.get('result') != 'done'):
        raise Unconfirmed('stop job or identity not confirmed')


class SyntheticService:
    """One attempted manager submission; uncertainty permanently closes it."""
    def __init__(self, backend, clock=time.monotonic):
        self.backend = backend
        self.clock = clock
        self.attempted = False

    def run(self, task, mode, stop_requested):
        if self.attempted:
            raise Unconfirmed('attempt already consumed; no resubmission')
        spec = synthetic_spec(task, mode)
        self.attempted = True
        outer = self.clock() + TOTAL_SECONDS
        start = min(outer, self.clock() + START_SECONDS)
        remaining(self.clock, start)
        try:
            job = self.backend.submit(spec, start)
        except Rejected:
            raise
        except BaseException as error:
            raise Unconfirmed('submission uncertain; do not resubmit or signal') from error
        remaining(self.clock, start)
        try:
            observed = self.backend.observe_start(spec['unit'], job, start)
        except BaseException as error:
            raise Unconfirmed('start observation unavailable; do not signal') from error
        remaining(self.clock, start)
        identity = bind(spec, job, observed)
        if observed.get('job_terminal') is not True or observed.get('start_result') != 'done':
            snapshot = capture(self.backend, identity, start, self.clock)
            if snapshot.get('evidence_saved') is not True:
                raise Unconfirmed('start failure evidence not saved')
            raise Unconfirmed('start job or executable outcome unconfirmed')
        runtime = min(outer, self.clock() + RUN_SECONDS)
        snapshot = capture(self.backend, identity, runtime, self.clock)
        if snapshot.get('evidence_saved') is not True:
            raise Unconfirmed('pre-stop evidence not saved')
        if snapshot.get('state') in ('inactive', 'failed'):
            require_settled(snapshot)
            return snapshot
        if snapshot.get('state') != 'active' or stop_requested is not True:
            raise Unconfirmed('active service not authorized for synthetic stop')
        stop = min(outer, self.clock() + STOP_SECONDS)
        remaining(self.clock, stop)
        try:
            response = self.backend.stop(identity, stop)
        except BaseException as error:
            raise Unconfirmed('stop outcome uncertain; never retry automatically') from error
        remaining(self.clock, stop)
        try:
            verify_stop(identity, response)
        except Unconfirmed as error:
            snapshot = capture(self.backend, identity, stop, self.clock)
            if snapshot.get('evidence_saved') is not True:
                raise Unconfirmed('stop failure evidence not saved') from error
            raise
        snapshot = capture(self.backend, identity, stop, self.clock)
        require_settled(snapshot)
        return snapshot
