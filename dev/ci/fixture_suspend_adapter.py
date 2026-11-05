"""Serialized fixture adapter; no package-manager or production integration."""
from dataclasses import dataclass
import math
import secrets
import threading
import time

from fixture_suspend_policy import Member, Observation, Policy, Result


@dataclass(frozen=True)
class FixtureIdentity:
    pid: int
    start: str
    parent: int
    group: int
    session: int


class FixtureInitializationError(RuntimeError):
    def __init__(self, fixture):
        super().__init__('fixture identity invalid; outer containment required')
        self.fixture = fixture


class FixtureAdapter:
    """A finite registry exists only after the fixture launcher creates it."""
    def __init__(self, launcher, operations, deadline_seconds=2):
        if (type(deadline_seconds) not in (int, float) or not math.isfinite(deadline_seconds) or
                deadline_seconds <= 0 or deadline_seconds > 10):
            raise ValueError('invalid native operation deadline')
        self._lock = threading.RLock()
        self._operations = operations
        self._timeout = deadline_seconds
        self._fixture = launcher.create(deadline_seconds)
        self._closed = False
        self._lost = False
        self._in_policy = False
        self._lease = secrets.token_hex(16)
        self._controller_pid = self._fixture.controller_pid
        self._controller_group = self._fixture.controller_group
        self._registry = tuple(self._fixture.identities)
        try:
            self._validate_registry()
            self._policy = Policy(self._registry[0].pid, self._registry[0].group,
                                  self._controller_pid, self._controller_group, self._lease,
                                  self, 64, time.monotonic() + deadline_seconds)
        except Exception as error:
            raise FixtureInitializationError(self._fixture) from error

    def _validate_registry(self):
        if len(self._registry) != 2 or any(not isinstance(item, FixtureIdentity) for item in self._registry):
            raise ValueError('fixture must own exactly leader and child')
        leader, child = self._registry
        if (leader.pid <= 1 or child.pid <= 1 or leader.pid == child.pid or
                leader.pid != leader.group or leader.pid != leader.session or
                leader.parent != self._controller_pid or child.parent != leader.pid or
                child.group != leader.group or child.session != leader.session or
                self._controller_group == leader.group or self._controller_pid in (leader.pid, child.pid) or
                not leader.start or not child.start):
            raise ValueError('fixture identities do not establish isolated ownership')

    def observe_without_reaping(self):
        with self._lock:
            return self._observe()

    def _observe(self):
        if self._closed or self._lost:
            raise RuntimeError('fixture ownership is unavailable')
        raw = self._operations.snapshot(self._fixture, self._registry, self._timeout)
        if raw.get('control_unchanged') is not True:
            raise RuntimeError('unrelated control process was affected or lost')
        source = dict(raw['members'])
        if set(source) != {item.pid for item in self._registry}:
            raise RuntimeError('fixture membership changed')
        for item in self._registry:
            member = source[item.pid]
            if (member['start'] != item.start or member['group'] != item.group or
                    member['session'] != item.session or member['parent'] != item.parent):
                raise RuntimeError('fixture identity changed')
        members = {int(pid): Member(str(value['start']), int(value['group']), str(value['state']))
                   for pid, value in source.items()}
        return Observation(self._registry[0].group, self._registry[0].pid, self._lease,
                           raw['leader_held'] is True, raw['visible'] is True,
                           raw['descendants_complete'] is True, raw['escaped'] is True, dict(members))

    def send_stop(self, group):
        with self._lock:
            if self._closed or self._lost or not self._in_policy or group != self._registry[0].group:
                raise RuntimeError('fixture target is no longer authorized')
            return self._operations.stop(self._fixture, self._registry, self._policy.deadline) is True

    def now(self):
        return time.monotonic()

    def storage_error(self, origin, error):
        with self._lock:
            if self._closed or self._lost:
                return Result.UNPROVEN
            self._in_policy = True
            try:
                result = self._policy.storage_error(origin, error)
            finally:
                self._in_policy = False
            if result in (Result.UNPROVEN, Result.CONTROLLER_STORAGE):
                self._lost = True
            return result

    def observation_lost(self):
        with self._lock:
            self._lost = True
            return self._policy.observation_lost()

    def teardown(self):
        with self._lock:
            if self._closed:
                return True
            if self._lost:
                return False
            self._lost = True  # A failed or ambiguous teardown cannot be retried.
            try:
                complete = self._operations.teardown(self._fixture, self._registry, self._timeout) is True
            except Exception:
                return False
            if not complete:
                return False
            self._closed = True
            return True
