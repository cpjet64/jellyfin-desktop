"""Injected systemd-255 client contract; contains no D-Bus implementation.

The client must keep one system-bus connection and buffer watched signals. Every
method receives the unchanged absolute monotonic deadline. This module is offline
until a separately reviewed client is explicitly supplied.
"""
import re

import linux_synthetic_service as model


def typed_properties(spec):
    properties = spec['properties']
    argv = [spec['executable'], *spec['arguments']]
    return [('Type', 's', properties['Type']), ('User', 's', properties['User']),
            ('Group', 's', properties['Group']), ('Restart', 's', properties['Restart']),
            ('Delegate', 'b', properties['Delegate']), ('KillMode', 's', properties['KillMode']),
            ('SendSIGKILL', 'b', properties['SendSIGKILL']), ('AddRef', 'b', True),
            ('TimeoutStartUSec', 't', properties['TimeoutStartSec'] * 1_000_000),
            ('TimeoutStopUSec', 't', properties['TimeoutStopSec'] * 1_000_000),
            ('RuntimeMaxUSec', 't', properties['RuntimeMaxSec'] * 1_000_000),
            ('WorkingDirectory', 's', spec['workspace']),
            ('Environment', 'as', [f'{key}={value}' for key, value in sorted(spec['environment'].items())]),
            ('ExecStart', 'a(sasb)', [(spec['executable'], argv, False)])]


def task_from_unit(unit):
    match = re.fullmatch(r'w644-([0-9a-f]{32})\.service', unit)
    if not match:
        raise ValueError('not a synthetic task unit')
    return match.group(1)


class SystemdAdapter:
    """One watched synthetic unit over an injected, persistent system-bus client."""
    def __init__(self, client):
        if client.transport != 'system-bus':
            raise ValueError('persistent system bus required')
        self.client = client
        self.spec = None
        self.start_attempted = False
        self.stop_attempted = False
        self.start_cursor = None
        self.stop_cursor = None
        self.binding = None
        self.observation_revision = -1

    def submit(self, spec, deadline):
        if self.start_attempted:
            raise model.Unconfirmed('start already attempted')
        task = task_from_unit(spec['unit'])
        mode = spec['arguments'][-1]
        if spec != model.synthetic_spec(task, mode):
            raise ValueError('synthetic specification changed')
        self.spec = spec
        if self.client.subscribe(deadline) is not True:
            raise model.Unconfirmed('manager subscription unconfirmed')
        self.start_cursor = self.client.watch_jobs(deadline)
        if type(self.start_cursor) is not int or self.start_cursor < 0:
            raise model.Unconfirmed('job watch cursor unavailable')
        if self.client.watch_unit(spec['unit'], deadline) is not True:
            raise model.Unconfirmed('unit watch unavailable')
        self.start_attempted = True
        return self.client.start_transient(spec['unit'], 'fail', typed_properties(spec), [], deadline)

    def observe_start(self, unit, job, deadline):
        if self.spec is None or unit != self.spec['unit']:
            raise ValueError('unit differs from submitted task')
        event = self.client.wait_job(self.start_cursor, job, deadline)
        identity = self.client.unit_identity(unit, deadline)
        observed = {**identity, 'unit': unit, 'job': event['job'], 'bus': 'system',
                    'job_observed': event['unit'] == unit and event['job'] == job,
                    'job_terminal': event['terminal'], 'start_result': event['result']}
        self.binding = model.bind(self.spec, job, observed)
        return observed

    def capture(self, identity, deadline):
        self._identity(identity)
        cursor = self.client.unit_cursor(identity['unit'], deadline)
        value = self.client.capture_evidence(identity['unit'], cursor, deadline)
        self._observation(value, cursor)
        return value, cursor

    def next_change(self, identity, cursor, deadline):
        self._identity(identity)
        next_cursor = self.client.wait_unit_change(identity['unit'], cursor, deadline)
        value = self.client.capture_evidence(identity['unit'], next_cursor, deadline)
        self._observation(value, next_cursor)
        return value, next_cursor

    def _observation(self, value, cursor):
        if (not isinstance(value, dict) or value.get('watch_cursor') != cursor or
                value.get('provenance') != 'independent-observer' or
                value.get('fixture_mode') != self.spec['arguments'][-1] or
                type(value.get('observation_revision')) is not int or
                value['observation_revision'] <= self.observation_revision):
            raise model.Unconfirmed('fresh case-specific observation required')
        self.observation_revision = value['observation_revision']

    def submit_stop(self, identity, deadline):
        self._identity(identity)
        if self.stop_attempted:
            raise model.Unconfirmed('stop already attempted')
        snapshot, _ = self.capture(identity, deadline)
        model.verified_snapshot(identity, snapshot)
        if (snapshot.get('state') != 'active' or snapshot.get('fixture_ready') is not True or
                snapshot.get('fixture_mode') != self.spec['arguments'][-1]):
            raise model.Unconfirmed('fresh matching readiness required before stop')
        self.stop_cursor = self.client.job_cursor(deadline)
        if type(self.stop_cursor) is not int or self.stop_cursor < 0:
            raise model.Unconfirmed('stop job watch cursor unavailable')
        self.stop_attempted = True
        return self.client.stop_unit(identity['unit'], 'fail', deadline)

    def observe_stop(self, identity, stop_job, deadline):
        self._identity(identity)
        event = self.client.wait_job(self.stop_cursor, stop_job, deadline)
        return {**identity, 'stop_job': stop_job, 'observed_job': event['job'],
                'observed_unit': event['unit'], 'job_observed': True,
                'job_terminal': event['terminal'], 'result': event['result']}

    def release(self, identity, snapshot, deadline):
        self._identity(identity)
        fresh, _ = self.capture(identity, deadline)
        model.verified_snapshot(identity, fresh)
        if any(fresh.get(key) != snapshot.get(key) for key in
               ('state', 'fixture_mode', 'fixture_result', 'job_terminal', 'cgroup_empty', 'descendants_absent')):
            raise model.Unconfirmed('settlement evidence changed before release')
        snapshot = fresh
        if (any(snapshot.get(key) != value for key, value in identity.items()) or
                snapshot.get('reference_held') is not True or snapshot.get('state') not in ('inactive', 'failed') or
                snapshot.get('evidence_saved') is not True or snapshot.get('cgroup_empty') is not True or
                snapshot.get('descendants_absent') is not True or snapshot.get('job_terminal') is not True):
            raise model.Unconfirmed('evidence and settlement required before release')
        return self.client.unref_unit(identity['unit'], deadline)

    def _identity(self, identity):
        if self.binding is None or identity != self.binding:
            raise ValueError('not the submitted synthetic unit')
