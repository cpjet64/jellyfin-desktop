"""Offline, fail-closed assembly for the pinned portable proof bundle."""
import hashlib
import os
from pathlib import Path
import re
import shutil
import stat

SOURCE = '8358bd61286fb2d706f245a278aa577ef27363e1'
TOTAL = 156208
CHUNKS = (
    (65536, '67f30c7723a2a8f9564f2db44e049082b9988fe4deb8e0f21a6bade196b64303'),
    (65536, '48e4e9540da4475c6a6a549cce92d1b23a55eeb357164faacc38b455b89bf2fc'),
    (25136, 'ce49a1527a009df3ac3e4b1dcdd648ea893acab637cb1de54dcd7f919703e076'),
)
SHA256 = 'ee9daf887e9a2841f22b3dda9dedeaa203d278eabdf570aea3177fdbd178f07c'

def digest(data):
    return hashlib.sha256(data).hexdigest()

def read_chunk(path, size, expected):
    flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0)
    fd = os.open(path, flags)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_nlink != 1 or info.st_size != size:
            raise ValueError('unsafe chunk')
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            data = stream.read(size + 1)
        if len(data) != size or digest(data) != expected:
            raise ValueError('chunk identity mismatch')
        return data
    finally:
        os.close(fd)

def workspace(parent, task):
    if not re.fullmatch('[0-9a-f]{32}', task):
        raise ValueError('invalid task identifier')
    root = Path(parent)
    validate_root(root)
    path = root / ('portable-proof-' + task)
    path.mkdir(mode=0o700)
    return path

def validate_root(root):
    if not root.is_absolute() or root.resolve(strict=True) != root:
        raise ValueError('noncanonical root')
    for item in (root, *root.parents):
        info = item.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid not in (0, os.getuid()) or info.st_mode & 0o022:
            raise ValueError('unsafe root ancestor')
    if root.stat().st_uid != os.getuid() or shutil.disk_usage(root).free < 2 * 1024**3:
        raise ValueError('unowned or full root')

def validate_workspace(path, task):
    if path.name != 'portable-proof-' + task or not re.fullmatch('[0-9a-f]{32}', task):
        raise ValueError('task identity mismatch')
    validate_root(path.parent)
    info = path.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
        raise ValueError('unsafe workspace')

def append(path, task, index, data):
    path = Path(path)
    validate_workspace(path, task)
    if type(index) is not int or not 0 <= index < len(CHUNKS):
        raise ValueError('invalid chunk index')
    if {item.name for item in path.iterdir()} != {f'chunk-{i}' for i in range(index)}:
        raise ValueError('unexpected workspace layout')
    size, expected = CHUNKS[index]
    if len(data) != size or digest(data) != expected:
        raise ValueError('invalid chunk bytes')
    for prior in range(index):
        read_chunk(path / f'chunk-{prior}', *CHUNKS[prior])
    with (path / f'chunk-{index}').open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    return {'index': index, 'sha256': expected}

def finish(path, task):
    path = Path(path)
    validate_workspace(path, task)
    if {item.name for item in path.iterdir()} != {f'chunk-{i}' for i in range(len(CHUNKS))}:
        raise ValueError('unexpected workspace layout')
    chunks = []
    for index, (size, expected) in enumerate(CHUNKS):
        chunks.append(read_chunk(path / f'chunk-{index}', size, expected))
    data = b''.join(chunks)
    if len(data) != TOTAL or digest(data) != SHA256:
        raise ValueError('bundle identity mismatch')
    with (path / 'source.bundle').open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    return path / 'source.bundle'
