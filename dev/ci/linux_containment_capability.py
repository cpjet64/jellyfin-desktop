"""Preparation-only, read-only Linux cgroup capability inspection."""
import json
import os
from pathlib import Path
import selectors
import signal
import socket
import subprocess
import sys
import time

MAX_OUTPUT = 16384


def read(path, limit=65536):
    with open(path, encoding='utf-8') as stream:
        value = stream.read(limit + 1)
    if len(value) > limit:
        raise ValueError('input too large')
    return value


def command(args, deadline, env=None):
    left = deadline - time.monotonic()
    if left <= 0:
        raise TimeoutError('inspection deadline')
    result, process = None, None
    safe_env = {'LANG': 'C', 'LC_ALL': 'C', 'PATH': '/usr/bin:/bin'}
    if env is not None:
        safe_env.update(env)
    try:
        process = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   env=safe_env)
        end = min(deadline, time.monotonic() + 2)
        data = {'stdout': bytearray(), 'stderr': bytearray()}
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ, 'stdout')
            selector.register(process.stderr, selectors.EVENT_READ, 'stderr')
            while selector.get_map():
                left = end - time.monotonic()
                if left <= 0:
                    raise TimeoutError('command deadline')
                for key, _ in selector.select(left):
                    chunk = os.read(key.fileobj.fileno(), 4096)
                    if not chunk:
                        selector.unregister(key.fileobj)
                    else:
                        data[key.data].extend(chunk)
                        if len(data[key.data]) > MAX_OUTPUT:
                            raise ValueError('output limit')
        code = process.wait(timeout=max(0, end - time.monotonic()))
        result = {'exit': code, **{key: value.decode('utf-8', 'replace') for key, value in data.items()}}
    except BaseException as error:
        result = {'error': type(error).__name__}
    finally:
        cleanup = []
        if process is not None:
            try:
                if process.poll() is None:
                    process.kill()
            except BaseException as error:
                cleanup.append(type(error).__name__)
            try:
                process.wait(timeout=2)
            except BaseException as error:
                cleanup.append(type(error).__name__)
            for stream in (process.stdout, process.stderr):
                try:
                    stream.close()
                except BaseException as error:
                    cleanup.append(type(error).__name__)
        if cleanup:
            result = {'error': (result or {}).get('error', 'cleanup failed'), 'cleanup_unproven': cleanup}
    return result


def metadata(path):
    try:
        info = os.stat(path)
        return {'uid': info.st_uid, 'gid': info.st_gid, 'mode': oct(info.st_mode & 0o7777),
                'read': os.access(path, os.R_OK), 'write': os.access(path, os.W_OK),
                'execute': os.access(path, os.X_OK)}
    except OSError as error:
        return {'error': type(error).__name__}


def optional_read(path):
    try:
        return read(path, 8192).strip()
    except OSError as error:
        return {'error': type(error).__name__}


def ancestors(cgroup):
    root = Path('/sys/fs/cgroup').resolve()
    target = (root / cgroup.lstrip('/')).resolve()
    if target != root and root not in target.parents:
        raise ValueError('cgroup outside root')
    result = []
    while True:
        result.append(target)
        if target == root:
            return result
        if len(result) >= 32:
            raise ValueError('cgroup ancestry too deep')
        target = target.parent


def group_info(path, deadline):
    return {'path': str(path), 'directory': metadata(path),
            'acl': command(['/usr/bin/getfacl', '-cp', str(path)], deadline),
            'controllers': optional_read(path / 'cgroup.controllers'),
            'subtree_control': optional_read(path / 'cgroup.subtree_control'),
            'events': optional_read(path / 'cgroup.events'), 'kill': metadata(path / 'cgroup.kill')}


def inspect_context(expected_uid, deadline):
    if os.geteuid() != expected_uid or os.getuid() != expected_uid or os.getgid() != os.getegid():
        raise ValueError('effective identity changed')
    memberships = read('/proc/self/cgroup', 8192).splitlines()
    unified = [line[3:] for line in memberships if line.startswith('0::/')]
    if len(unified) != 1 or '\\' in unified[0] or any(part in ('.', '..') for part in unified[0].split('/')):
        raise ValueError('unique unified cgroup unavailable')
    mounts = read('/proc/self/mountinfo', 131072).splitlines()
    matches = [line for line in mounts if ' - cgroup2 ' in line]
    if (len(matches) != 1 or matches[0].split()[3:5] != ['/', '/sys/fs/cgroup'] or
            '\\' in matches[0].split()[3] or '\\' in matches[0].split()[4]):
        raise ValueError('cgroup v2 mount ambiguous')
    groups = [group_info(path, deadline) for path in ancestors(unified[0])]
    unit = command(['/usr/bin/systemctl', 'show', '--property=ControlGroup,Delegate,User,KillMode,RemainAfterExit,CollectMode',
                    'gitlab-runner.service'], deadline)
    user_slice = command(['/usr/bin/systemctl', 'show', '--property=ControlGroup,Delegate,User,KillMode,RemainAfterExit,CollectMode',
                          'user-%d.slice' % expected_uid], deadline)
    runtime = '/run/user/%d' % expected_uid
    user = command(['/usr/bin/systemctl', '--user', 'show', '--property=Version'], deadline,
                   env={'XDG_RUNTIME_DIR': runtime, 'DBUS_SESSION_BUS_ADDRESS': 'unix:path=%s/bus' % runtime})
    return {'uid': os.getuid(), 'euid': os.geteuid(), 'gid': os.getgid(), 'egid': os.getegid(),
            'groups': os.getgroups(), 'pid': os.getpid(), 'ppid': os.getppid(),
            'session': os.getsid(0), 'process_group': os.getpgrp(), 'membership': memberships,
            'mount': matches[0], 'ancestors': groups, 'system_unit': unit, 'user_slice': user_slice,
            'user_manager': user, 'system_socket': metadata('/run/systemd/private'),
            'user_socket': metadata(runtime + '/systemd/private'), 'user_bus': metadata(runtime + '/bus')}


def alarm(_signum, _frame):
    raise TimeoutError('inspection deadline')


def main():
    import pwd
    if len(sys.argv) != 3 or sys.argv[1] not in ('controller', 'runner'):
        raise ValueError('fixed role and expected UID required')
    uid = int(sys.argv[2])
    if uid < 0 or (sys.argv[1] == 'controller' and uid != 0) or (sys.argv[1] == 'runner' and uid == 0):
        raise ValueError('invalid role identity')
    signal.signal(signal.SIGALRM, alarm)
    signal.alarm(20)
    deadline = time.monotonic() + 18
    release = read('/etc/os-release', 8192).splitlines()
    os_info = {key: value.strip('"') for line in release if '=' in line
               for key, value in [line.split('=', 1)] if key in ('ID', 'VERSION_ID')}
    result = {'role': sys.argv[1], 'hostname': socket.gethostname(), 'account': pwd.getpwuid(uid).pw_name,
              'os': os_info,
              'kernel': list(os.uname()), 'pid1': optional_read('/proc/1/comm'),
              'systemd_version': command(['/usr/bin/systemd', '--version'], deadline),
              'systemd_run': metadata('/usr/bin/systemd-run'), 'context': inspect_context(uid, deadline)}
    data = json.dumps(result, sort_keys=True)
    if len(data.encode()) > 65536:
        raise ValueError('report too large')
    print(data, flush=True)


if __name__ == '__main__':
    main()
