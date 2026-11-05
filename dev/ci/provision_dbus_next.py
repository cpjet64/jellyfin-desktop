"""Provision the single pinned wheel into task-local ignored storage."""
import hashlib
import os
from pathlib import Path
import re
import subprocess

URL = 'https://files.pythonhosted.org/packages/d2/fc/c0a3f4c4eaa5a22fbef91713474666e13d0ea2a69c84532579490a9f2cc8/dbus_next-0.2.3-py3-none-any.whl'
SHA = '58948f9aff9db08316734c0be2a120f6dc502124d9642f55e90ac82ffb16a18b'
SIZE = 57885
ROOT = Path(__file__).resolve().parents[2]
WHEEL = ROOT / 'deps' / 'w693-dbus-next' / 'dbus_next-0.2.3-py3-none-any.whl'


def verify(data):
    if len(data) != SIZE or hashlib.sha256(data).hexdigest() != SHA:
        raise RuntimeError('wheel size or digest mismatch')


def check_path(path):
    expected = ROOT / 'deps' / 'w693-dbus-next' / 'dbus_next-0.2.3-py3-none-any.whl'
    if path != expected:
        raise RuntimeError('unexpected artifact path')
    for item in (ROOT / 'deps', path.parent, path):
        if item.is_symlink() or (hasattr(item, 'is_junction') and item.is_junction()):
            raise RuntimeError('reparse artifact path')
    if (ROOT / 'deps').resolve() != ROOT / 'deps':
        raise RuntimeError('artifact root changed')


def read_bounded(path):
    with path.open('rb') as source:
        return source.read(SIZE + 1)


def download(run=subprocess.run):
    options = {'stdout': subprocess.PIPE, 'stderr': subprocess.DEVNULL, 'check': True,
               'creationflags': subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0}
    version = run(['curl', '--disable', '--version'], timeout=10, **options).stdout
    match = re.match(rb'curl (\d+)\.(\d+)\.(\d+)\b', version)
    if not match or tuple(map(int, match.groups())) < (8, 4, 0):
        raise RuntimeError('curl 8.4.0 or newer required for streaming size limit')
    result = run(['curl', '--disable', '--fail', '--silent', '--show-error', '--proto', '=https',
                  '--max-time', '60', '--max-filesize', '1048576', URL], timeout=60, **options)
    return result.stdout


def provision(path=WHEEL, fetch=download):
    check_path(path)
    if path.exists():
        if path.stat().st_size != SIZE:
            raise RuntimeError('existing wheel size mismatch')
        verify(read_bounded(path))
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    check_path(path)
    if path.parent.resolve() != ROOT / 'deps' / 'w693-dbus-next':
        raise RuntimeError('artifact directory resolves outside fixed location')
    data = fetch()
    verify(data)
    with path.open('xb') as target:
        target.write(data)
    verify(read_bounded(path))
    return path


if __name__ == '__main__':
    print(provision())
