"""Apply one journaled transfer operation in an already authorized runner context."""
import base64
import hashlib
from pathlib import Path
import assembly

def apply(parent, task, operation, args):
    root = Path(parent)
    path = root / ('portable-proof-' + task)
    if operation == 'init' and not args:
        assembly.workspace(root, task)
        return {'offset': 0, 'sha256': hashlib.sha256(b'').hexdigest()}
    assembly.validate_workspace(path, task)
    if operation == 'append' and len(args) == 3:
        return append(path, task, args)
    if operation == 'finish' and not args:
        result = assembly.finish(path, task)
        return {'offset': result.stat().st_size, 'sha256': assembly.SHA256}
    raise ValueError('invalid transfer operation')

def append(path, task, args):
    offset_text, previous, encoded = args
    offset = int(offset_text)
    sizes = [part[0] for part in assembly.CHUNKS]
    offsets = [0, sizes[0], sizes[0] + sizes[1]]
    if offset not in offsets:
        raise ValueError('invalid chunk offset')
    index = offsets.index(offset)
    prefix = b''.join(assembly.read_chunk(path / f'chunk-{i}', *assembly.CHUNKS[i]) for i in range(index))
    if hashlib.sha256(prefix).hexdigest() != previous:
        raise ValueError('prefix hash mismatch')
    if len(encoded) > 90000:
        raise ValueError('encoded chunk exceeds limit')
    data = base64.b64decode(encoded, validate=True)
    assembly.append(path, task, index, data)
    prefix += data
    return {'offset': len(prefix), 'sha256': hashlib.sha256(prefix).hexdigest()}
