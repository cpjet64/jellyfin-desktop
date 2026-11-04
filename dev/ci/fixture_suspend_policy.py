"""Fixture-only suspension decision policy; it contains no OS signalling adapter."""
from dataclasses import dataclass
from enum import Enum
import errno
import math


class Result(Enum):
    IGNORED = 'ignored'
    CONTROLLER_STORAGE = 'controller-storage'
    STOPPED = 'observed-stopped'
    UNPROVEN = 'unproven'


@dataclass(frozen=True)
class Member:
    identity: str
    group: int
    state: str


@dataclass(frozen=True)
class Observation:
    group: int
    leader: int
    lease: str
    leader_held: bool
    visible: bool
    descendants_complete: bool
    escaped: bool
    members: dict


def storage_failure(error):
    codes = {errno.ENOSPC}
    if hasattr(errno, 'EDQUOT'):
        codes.add(errno.EDQUOT)
    pending, seen = [error], set()
    while pending and len(seen) < 64:
        current = pending.pop()
        if not isinstance(current, BaseException) or id(current) in seen:
            continue
        seen.add(id(current))
        try:
            if isinstance(current, OSError) and current.errno in codes:
                return True
            pending.extend((current.__cause__, current.__context__, getattr(current, 'reason', None)))
        except Exception:
            return None
    return None if pending else False


class Policy:
    def __init__(self, leader, group, controller_pid, controller_group, lease, backend, max_samples, deadline):
        ids = (leader, group, controller_pid, controller_group)
        if (any(type(value) is not int or value <= 1 for value in ids) or leader != group or
                controller_pid in (leader, group) or controller_group == group or
                not isinstance(lease, str) or not lease or type(max_samples) is not int or not 1 <= max_samples <= 128 or
                type(deadline) not in (int, float) or not math.isfinite(deadline) or deadline <= 0):
            raise ValueError('invalid fixture-owned process group')
        self.leader, self.group, self.controller_pid = leader, group, controller_pid
        self.lease, self.backend, self.max_samples, self.deadline = lease, backend, max_samples, deadline
        self.invalidated = False

    def _validate(self, value, initial=None):
        if (not isinstance(value, Observation) or value.group != self.group or value.leader != self.leader or
                value.lease != self.lease or value.leader_held is not True or value.visible is not True or
                value.descendants_complete is not True or value.escaped is not False or
                not isinstance(value.members, dict) or self.leader not in value.members or
                self.controller_pid in value.members):
            return False
        for pid, member in value.members.items():
            if (type(pid) is not int or pid <= 1 or not isinstance(member, Member) or
                    not isinstance(member.identity, str) or not member.identity or member.group != self.group):
                return False
        if initial is not None and any(pid not in value.members or value.members[pid].identity != member.identity
                                       for pid, member in initial.members.items()):
            return False
        return True

    def storage_error(self, origin, error):
        if self.invalidated:
            return Result.UNPROVEN
        storage = storage_failure(error)
        if storage is None:
            self.invalidated = True
            return Result.UNPROVEN
        if origin == 'controller' and storage:
            self.invalidated = True
            return Result.CONTROLLER_STORAGE
        if origin not in ('guest', 'controller') and storage:
            self.invalidated = True
            return Result.UNPROVEN
        if origin != 'guest' or not storage:
            return Result.IGNORED
        self.invalidated = True  # No second signal, including after observation or delivery failure.
        try:
            if not self._within_deadline():
                return Result.UNPROVEN
            initial = self.backend.observe_without_reaping()
            if not self._validate(initial):
                return Result.UNPROVEN
            if not self._within_deadline():
                return Result.UNPROVEN
            if self.backend.send_stop(self.group) is not True:
                return Result.UNPROVEN
            for _ in range(self.max_samples):
                if not self._within_deadline():
                    return Result.UNPROVEN
                current = self.backend.observe_without_reaping()
                if not self._within_deadline() or not self._validate(current, initial):
                    return Result.UNPROVEN
                if all(member.state in ('T', 't') for member in current.members.values()):
                    return Result.STOPPED
        except Exception:
            return Result.UNPROVEN
        return Result.UNPROVEN

    def _within_deadline(self):
        now = self.backend.now()
        return type(now) in (int, float) and math.isfinite(now) and now < self.deadline

    def observation_lost(self):
        self.invalidated = True
        return Result.UNPROVEN
