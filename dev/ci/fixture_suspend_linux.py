"""Linux-only synthetic process launcher and bounded probe; never used for apt."""
import json
import math
import os
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import time
from types import SimpleNamespace


def remaining(deadline):
    if type(deadline) not in (int, float) or not math.isfinite(deadline):
        raise ValueError('invalid absolute deadline')
    now = time.monotonic()
    if not math.isfinite(now) or now >= deadline:
        raise TimeoutError('fixture deadline expired')
    return deadline - now


def proc_stat(pid):
    path = Path('/proc') / str(pid) / 'stat'
    with path.open(encoding='ascii') as source:
        text = source.read(8193)
    if len(text) > 8192 or ')' not in text:
        raise ValueError('unexpected process stat')
    fields = text[text.rfind(')') + 2:].split()
    return {'state': fields[0], 'parent': int(fields[1]), 'group': int(fields[2]),
            'session': int(fields[3]), 'start': fields[19]}


def inspect_registry(request):
    registry = request['registry']
    if len(registry) != 2 or len({item['pid'] for item in registry}) != 2:
        raise ValueError('invalid fixture registry')
    leader, child = registry
    controller, controller_group = request['controller'], request['controller_group']
    if (leader['pid'] <= 1 or leader['pid'] != leader['group'] or leader['pid'] != leader['session'] or
            leader['parent'] != controller or child['parent'] != leader['pid'] or
            child['group'] != leader['group'] or child['session'] != leader['session'] or
            controller_group == leader['group'] or controller in (leader['pid'], child['pid'])):
        raise ValueError('unsafe target identity')
    members = {}
    for item in registry:
        actual = proc_stat(item['pid'])
        if any(actual[key] != item[key] for key in ('parent', 'group', 'session', 'start')):
            raise ValueError('fixture identity changed')
        members[item['pid']] = actual
    if members[leader['pid']]['state'] == 'Z':
        raise ValueError('leader exited; no signal permitted')
    return members


def inspect_control(request):
    item = request['control']
    actual = proc_stat(item['pid'])
    if (item['pid'] <= 1 or item['pid'] in (request['controller'], request['registry'][0]['pid']) or
            item['group'] == request['registry'][0]['group'] or item['parent'] != request['controller'] or
            any(actual[key] != item[key] for key in ('parent', 'group', 'session', 'start')) or
            actual['state'] not in ('R', 'S')):
        raise ValueError('unrelated control identity or state changed')
    return True


def process_set():
    records = {}
    for path in Path('/proc').iterdir():
        if not path.name.isdigit():
            continue
        try:
            records[int(path.name)] = proc_stat(int(path.name))
        except FileNotFoundError:
            continue
        if len(records) > 32768:
            raise ValueError('process inventory exceeds fixture limit')
    return records


def verify_proc():
    if (os.readlink('/proc/1/ns/pid') != 'pid:[4026531836]' or
            os.readlink('/proc/self/ns/pid') != 'pid:[4026531836]'):
        raise ValueError('initial PID namespace is not established')
    lines = Path('/proc/self/mountinfo').read_text().splitlines()
    mounts = [line.split(' - ', 1) for line in lines if ' - ' in line]
    matches = [(left.split(), right.split()) for left, right in mounts if left.split()[4] == '/proc']
    if len(matches) != 1:
        raise ValueError('process mount is ambiguous')
    left, right = matches[0]
    options = set(left[5].split(',')) | set(right[2].split(','))
    if left[3] != '/' or right[0] != 'proc' or 'subset=pid' in options or any(x.startswith('hidepid=') and x != 'hidepid=0' for x in options):
        raise ValueError('process visibility is restricted')


def probe(request):
    verify_proc()
    if request['controller'] != os.getppid():
        raise ValueError('fixture controller identity changed')
    members = inspect_registry(request)
    inspect_control(request)
    registry = request['registry']
    leader = registry[0]['pid']
    records = process_set()
    owned = set(members)
    descendants = {leader}
    for _ in range(len(records) + 1):
        found = descendants | {pid for pid, value in records.items() if value['parent'] in descendants}
        if found == descendants:
            break
        descendants = found
    group = {pid for pid, value in records.items() if value['group'] == leader}
    complete = descendants == owned and group == owned
    members = inspect_registry(request)
    inspect_control(request)
    if request['operation'] in ('stop', 'teardown'):
        if not complete:
            raise ValueError('fixture descendants are not fully accounted')
        inspect_registry(request)  # Revalidate immediately before signalling.
        remaining(request['deadline'])
        os.killpg(leader, signal.SIGSTOP if request['operation'] == 'stop' else signal.SIGKILL)
        return {'sent': True}
    return {'members': members, 'leader_held': True, 'visible': True,
            'descendants_complete': complete, 'escaped': not complete, 'control_unchanged': True}


def helper(operation, fixture, registry, deadline):
    if signal.getsignal(signal.SIGCHLD) != signal.SIG_DFL:
        raise RuntimeError('fixture leader reaping policy changed')
    request = {'operation': operation, 'controller': fixture.controller_pid,
               'controller_group': fixture.controller_group,
               'registry': [item.__dict__ for item in registry], 'control': fixture.control_identity.__dict__,
               'deadline': deadline}
    result = subprocess.run([sys.executable, '-I', '-B', __file__, '--probe', json.dumps(request)],
                            stdin=subprocess.DEVNULL, capture_output=True, timeout=remaining(deadline), text=True)
    if result.returncode or len(result.stdout) > 65536 or len(result.stderr) > 65536:
        raise RuntimeError('bounded fixture probe failed')
    value = json.loads(result.stdout)
    if 'members' in value:
        value['members'] = {int(key): item for key, item in value['members'].items()}
    return value


class Operations:
    def __init__(self, launcher):
        if type(launcher) is not Launcher:
            raise ValueError('operations require the native fixture launcher')
        self._launcher = launcher

    def _check(self, fixture, registry):
        if (fixture is not self._launcher._issued or tuple(registry) != self._launcher._registry or
                fixture.control_identity != self._launcher._control or fixture.leader is not self._launcher._leader or
                fixture.control is not self._launcher._control_process):
            raise ValueError('fixture registry was not issued by this launcher')

    def snapshot(self, fixture, registry, timeout):
        self._check(fixture, registry)
        return helper('snapshot', fixture, registry, time.monotonic() + timeout)

    def stop(self, fixture, registry, deadline):
        self._check(fixture, registry)
        return helper('stop', fixture, registry, deadline).get('sent') is True

    def teardown(self, fixture, registry, timeout):
        self._check(fixture, registry)
        deadline = time.monotonic() + timeout
        if helper('teardown', fixture, registry, deadline).get('sent') is not True:
            return False
        while time.monotonic() < deadline:
            result = subprocess.run([sys.executable, '-I', '-B', __file__, '--settled',
                                     str(registry[0].pid), str(registry[1].pid), registry[1].start],
                                    capture_output=True, timeout=max(.01, deadline - time.monotonic()), text=True)
            if result.returncode == 0 and result.stdout.strip() == 'settled':
                if signal.getsignal(signal.SIGCHLD) != signal.SIG_DFL:
                    return False
                status = os.waitid(os.P_PID, fixture.leader.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
                if status is None or status.si_pid != fixture.leader.pid:
                    continue
                fixture.leader.wait(timeout=max(.01, deadline - time.monotonic()))
                control = fixture.control_identity
                stopped = subprocess.run([sys.executable, '-I', '-B', __file__, '--control-stop',
                                          str(control.pid), control.start, str(control.parent)],
                                         capture_output=True, timeout=max(.01, deadline - time.monotonic()), text=True)
                if stopped.returncode:
                    return False
                fixture.control.wait(timeout=max(.01, deadline - time.monotonic()))
                return True
            time.sleep(.02)
        return False


class FixtureLaunchError(RuntimeError):
    def __init__(self, leader, control, child_pid, identities):
        super().__init__('fixture launch incomplete; explicit containment required')
        self.leader = leader
        self.control = control
        self.child_pid = child_pid
        self.identities = tuple(identities)


class Launcher:
    def __init__(self):
        self._issued = None
        self._registry = None
        self._control = None
        self._leader = None
        self._control_process = None
        self._launch_attempted = False

    def create(self, timeout):
        if (self._launch_attempted or sys.platform != 'linux' or
                type(timeout) not in (int, float) or not math.isfinite(timeout) or timeout <= 0 or timeout > 10 or
                signal.getsignal(signal.SIGCHLD) != signal.SIG_DFL):
            raise ValueError('native Linux fixture and bounded deadline required')
        self._launch_attempted = True
        deadline = time.monotonic() + timeout
        from fixture_suspend_adapter import FixtureIdentity
        leader = subprocess.Popen([sys.executable, '-I', '-B', __file__, '--leader'],
                                  stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, start_new_session=True)
        control = None
        child = None
        identities = []
        try:
            selector = selectors.DefaultSelector()
            try:
                selector.register(leader.stdout, selectors.EVENT_READ)
                if not selector.select(remaining(deadline)):
                    raise TimeoutError('fixture launch handshake timed out')
                raw = os.read(leader.stdout.fileno(), 256)
            finally:
                selector.close()
            child = json.loads(raw.decode())['child']
            for pid in (leader.pid, child):
                result = subprocess.run([sys.executable, '-I', '-B', __file__, '--stat', str(pid)],
                                        capture_output=True, timeout=remaining(deadline), text=True, check=True)
                info = json.loads(result.stdout)
                identities.append(FixtureIdentity(pid, info['start'], info['parent'], info['group'], info['session']))
            remaining(deadline)
            control = subprocess.Popen([sys.executable, '-I', '-B', __file__, '--control'],
                                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL, start_new_session=True)
            result = subprocess.run([sys.executable, '-I', '-B', __file__, '--stat', str(control.pid)],
                                    capture_output=True, timeout=remaining(deadline), text=True, check=True)
            info = json.loads(result.stdout)
            control_id = FixtureIdentity(control.pid, info['start'], info['parent'], info['group'], info['session'])
            remaining(deadline)
            fixture = SimpleNamespace(leader=leader, control=control, control_identity=control_id,
                                      identities=tuple(identities), controller_pid=os.getpid(), controller_group=os.getpgrp())
            self._issued, self._registry, self._control = fixture, fixture.identities, control_id
            self._leader, self._control_process = leader, control
            return fixture
        except BaseException as error:
            # Keep every known handle unreaped for explicit outer containment.
            raise FixtureLaunchError(leader, control, child, identities) from error


def leader_main():
    child = os.fork()
    if child == 0:
        while True:
            signal.pause()
    print(json.dumps({'child': child}), flush=True)
    while True:
        signal.pause()


if __name__ == '__main__':
    if len(sys.argv) == 2 and sys.argv[1] == '--leader':
        leader_main()
    elif len(sys.argv) == 2 and sys.argv[1] == '--control':
        while True:
            signal.pause()
    elif len(sys.argv) == 3 and sys.argv[1] == '--probe':
        print(json.dumps(probe(json.loads(sys.argv[2])), sort_keys=True), flush=True)
    elif len(sys.argv) == 3 and sys.argv[1] == '--stat':
        print(json.dumps(proc_stat(int(sys.argv[2]))), flush=True)
    elif len(sys.argv) == 5 and sys.argv[1] == '--settled':
        leader, child, start = int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
        records = process_set()
        group = {pid for pid, item in records.items() if item['group'] == leader and pid != leader}
        if not group and (child not in records or records[child]['start'] != start):
            print('settled', flush=True)
        else:
            raise SystemExit('fixture descendants still present')
    elif len(sys.argv) == 5 and sys.argv[1] == '--control-stop':
        pid, start, parent = int(sys.argv[2]), sys.argv[3], int(sys.argv[4])
        actual = proc_stat(pid)
        if pid <= 1 or actual['start'] != start or actual['parent'] != parent or parent != os.getppid():
            raise ValueError('control ownership changed')
        os.kill(pid, signal.SIGKILL)
    else:
        raise SystemExit('fixture helper mode required')
