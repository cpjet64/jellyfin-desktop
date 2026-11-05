"""Construct noninteractive Linux transfer requests; no network operations."""
import base64
import hashlib
import json
from pathlib import Path
import zlib
from urllib.parse import urlencode

HERE = Path(__file__).resolve().parent
MAX_REQUEST = 100000
PINS = {
    'assembly': '844899c64231fec5b8f4bdcedc3e8053b4912e4e92901b3e3beb7381741fe84f',
    'transfer': 'a118dc2102378aa541e1e591d5e103e425ae0c309c850411b6e2c8d65aa52269',
    'executor': '6693abedbf9dbc02ae87d340a29178a142b5f996e59dace38f6821f7ed65d2d0',
    'suite_runner.py': '31cb7fe63bdcc4186bdef12d1440429dfae8d9009a5b3c338a36517f01fa45f1',
    'manifest-8358bd6.json': '190d002c29585a051adec1fed896fdaffd7adc65ec38affec7840271f159d11b',
}

def checked_sources(names):
    sources = {name: (HERE / (name if '.' in name else name + '.py')).read_bytes().decode('utf-8') for name in names}
    pins = {name: PINS[name] for name in names}
    if any(hashlib.sha256(sources[name].encode()).hexdigest() != expected for name, expected in pins.items()):
        raise ValueError('local helper identity mismatch')
    return sources, pins
BOOTSTRAP = '''import base64,hashlib,json,os,platform,pwd,sys,types,zlib
if platform.system() != "Linux" or platform.machine().lower() != "x86_64": raise RuntimeError("unexpected platform")
account = pwd.getpwnam("gitlab-runner")
if os.getuid() != account.pw_uid or os.geteuid() != account.pw_uid: raise RuntimeError("runner identity mismatch")
src = json.loads(zlib.decompress(base64.b64decode(sys.argv[1],validate=True)))
expected = EXPECTED_PINS
if set(src) != set(expected) or any(hashlib.sha256(src[name].encode()).hexdigest() != value for name,value in expected.items()): raise RuntimeError("helper identity mismatch")
assembly = types.ModuleType("assembly")
sys.modules["assembly"] = assembly
exec(src["assembly"],assembly.__dict__)
transfer = types.ModuleType("transfer")
exec(src["transfer"],transfer.__dict__)
print(json.dumps(transfer.apply(account.pw_dir,sys.argv[2],sys.argv[3],sys.argv[4:]),sort_keys=True))
'''

def transfer_command(task, operation, args):
    sources, pins = checked_sources(('assembly', 'transfer'))
    packed = base64.b64encode(zlib.compress(json.dumps(sources, sort_keys=True).encode(), 9)).decode()
    command = ['/usr/sbin/runuser', '-u', 'gitlab-runner', '--', '/usr/bin/python3', '-I', '-S', '-B',
               '-c', BOOTSTRAP.replace('EXPECTED_PINS', repr(pins)), packed, task, operation, *args]
    data = urlencode({'command': command}, doseq=True).encode()
    if len(data) > MAX_REQUEST:
        raise ValueError('serialized request exceeds limit')
    return command, data

EXECUTE = '''import base64,hashlib,json,os,platform,pwd,stat,sys,types,zlib
if platform.system() != "Linux" or platform.machine().lower() != "x86_64": raise RuntimeError("unexpected platform")
account = pwd.getpwnam("gitlab-runner")
if os.getuid() != account.pw_uid or os.geteuid() != account.pw_uid: raise RuntimeError("runner identity mismatch")
src = json.loads(zlib.decompress(base64.b64decode(sys.argv[1],validate=True)))
expected = EXPECTED_PINS
if set(src) != set(expected) or any(hashlib.sha256(src[name].encode()).hexdigest() != value for name,value in expected.items()): raise RuntimeError("helper identity mismatch")
from pathlib import Path
interpreter = Path(sys.executable).resolve(strict=True)
info = interpreter.stat()
if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022 or not os.access(interpreter,os.X_OK): raise RuntimeError("unsafe Python")
if any(p.stat().st_uid != 0 or p.stat().st_mode & 0o022 for p in interpreter.parents): raise RuntimeError("unsafe Python ancestor")
if hashlib.sha256(interpreter.read_bytes()).hexdigest() != sys.argv[3]: raise RuntimeError("Python preflight mismatch")
assembly = types.ModuleType("assembly")
sys.modules["assembly"] = assembly
exec(src["assembly"],assembly.__dict__)
task = sys.argv[2]
workspace = Path(account.pw_dir) / ("portable-proof-" + task)
assembly.validate_workspace(workspace,task)
assembly.read_chunk(workspace / "source.bundle",assembly.TOTAL,assembly.SHA256)
runtime = workspace / "runtime"
runtime.mkdir(mode=0o700)
for name in ("manifest-8358bd6.json","suite_runner.py"):
    with (runtime / name).open("xb") as output:
        output.write(src[name].encode())
        output.flush()
        os.fsync(output.fileno())
sys.argv = [str(runtime / "executor.py"),task,sys.argv[3]]
exec(compile(src["executor"],sys.argv[0],"exec"),{"__name__":"__main__","__file__":sys.argv[0]})
'''

def execution_command(task, python_sha256):
    sources, pins = checked_sources(('assembly', 'executor', 'manifest-8358bd6.json', 'suite_runner.py'))
    packed = base64.b64encode(zlib.compress(json.dumps(sources, sort_keys=True).encode(), 9)).decode()
    command = ['/usr/sbin/runuser', '-u', 'gitlab-runner', '--', '/usr/bin/python3', '-I', '-S', '-B',
               '-c', EXECUTE.replace('EXPECTED_PINS', repr(pins)), packed, task, python_sha256]
    data = urlencode({'command': command}, doseq=True).encode()
    if len(data) > MAX_REQUEST:
        raise ValueError('serialized request exceeds limit')
    return command, data
