"""Verify one pinned Web source and run its offline producer contract tests."""
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
URL = ('https://raw.githubusercontent.com/jellyfin/jellyfin-web/'
       'ea2abad3e1671473d352b3ccf06f616c61ec9381/src/components/playback/playbackmanager.js')
SIZE = 158528
LIMIT = 256 * 1024
SHA = '43a9837e11041ddd624a6f68ee3fe3ffb7ee48dfa1443b27592a67c0c0895af2'
CACHE = ROOT / 'deps' / 'ci-jellyfin-web' / 'playbackmanager.js'
FLAGS = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0


def verify(data):
    if len(data) != SIZE or hashlib.sha256(data).hexdigest() != SHA:
        raise RuntimeError('source size or digest mismatch')


def check_path(path):
    if path != ROOT / 'deps' / 'ci-jellyfin-web' / 'playbackmanager.js':
        raise RuntimeError('unexpected cache path')
    for part in (ROOT / 'deps', path.parent, path):
        if part.is_symlink() or (hasattr(part, 'is_junction') and part.is_junction()):
            raise RuntimeError('linked cache path')
    if (ROOT / 'deps').resolve() != ROOT / 'deps':
        raise RuntimeError('cache root changed')


def read_verified(path):
    check_path(path)
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_size != SIZE:
        raise RuntimeError('cache is not the expected regular file')
    flags = os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0)
    with os.fdopen(os.open(path, flags), 'rb') as source:
        opened = os.fstat(source.fileno())
        if (not stat.S_ISREG(opened.st_mode) or opened.st_nlink != 1 or opened.st_size != SIZE or
                (opened.st_dev, opened.st_ino) != (info.st_dev, info.st_ino)):
            raise RuntimeError('cache changed during open')
        data = source.read(LIMIT + 1)
    verify(data)
    return data


def download(run=subprocess.run, clock=time.monotonic):
    options = {'stdout': subprocess.PIPE, 'stderr': subprocess.DEVNULL, 'check': True, 'creationflags': FLAGS}
    deadline = clock() + 60
    version = run(['curl', '--disable', '--version'], timeout=10, **options).stdout
    match = re.match(rb'curl (\d+)\.(\d+)\.(\d+)\b', version)
    if not match or tuple(map(int, match.groups())) < (8, 4, 0):
        raise RuntimeError('curl 8.4.0 or newer required')
    remaining = deadline - clock()
    if remaining <= 0:
        raise subprocess.TimeoutExpired('curl', 60)
    return run(['curl', '--disable', '--fail', '--silent', '--show-error', '--proto', '=https',
                '--max-time', str(remaining), '--max-filesize', str(LIMIT), URL], timeout=remaining, **options).stdout


def provision(path=CACHE, fetch=download):
    check_path(path)
    if path.exists():
        read_verified(path)
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    check_path(path)
    if path.parent.resolve() != ROOT / 'deps' / 'ci-jellyfin-web':
        raise RuntimeError('cache directory changed')
    data = fetch()
    verify(data)
    publish_verified(path, data)
    read_verified(path)
    return path


def publish_verified(path, data):
    # The final name appears atomically and cannot replace an existing cache.
    fd, temporary = tempfile.mkstemp(prefix='.verified-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.link(temporary, path)
    except OSError as error:
        # Preserve evidence on storage exhaustion; no dependent write or cleanup.
        import errno
        if error.errno in (errno.ENOSPC, getattr(errno, 'EDQUOT', -1)):
            raise
        os.unlink(temporary)
        raise
    os.unlink(temporary)


def normalized_hash(path):
    return hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest()


def report(path=CACHE, run=subprocess.run):
    read_verified(path)
    result = run(['git', 'rev-parse', 'HEAD'], cwd=ROOT, check=True, capture_output=True,
                 text=True, timeout=10, creationflags=FLAGS)
    evidence = {'checkout_sha': result.stdout.strip(), 'upstream_sha256': SHA,
                'patch_sha256_lf': normalized_hash(ROOT / 'patches/jellyfin-web/0001-recovery-transaction.patch'),
                'helper_sha256_lf': normalized_hash(ROOT / 'patches/jellyfin-web/recoveryTransaction.js')}
    print(json.dumps(evidence, sort_keys=True), flush=True)


if __name__ == '__main__':
    report(provision())
