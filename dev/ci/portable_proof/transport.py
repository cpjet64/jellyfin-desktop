"""Build bounded requests and preserve the first terminal response before decoding."""
from pathlib import Path
import json
from urllib.parse import urlencode
from qga_terminal import poll_preserving_terminal

MAX_REQUEST = 100000

def request_body(command):
    if not command or any(not isinstance(item, str) for item in command):
        raise ValueError('invalid command')
    data = urlencode({'command': command}, doseq=True).encode('utf-8')
    if len(data) > MAX_REQUEST:
        raise ValueError('serialized request exceeds limit')
    return data

def poll(pid, receipt, open_response):
    path = Path(receipt)
    return poll_preserving_terminal(pid, path, open_response)

class TerminalFailure(RuntimeError):
    terminal = True

    def __init__(self, message, metadata):
        super().__init__(message)
        self.metadata = metadata

def terminal_result(pid, receipt, open_response):
    status = poll(pid, receipt, open_response)
    if not status['exited']:
        return None
    metadata = {key: status.get(key) for key in ('exitcode', 'out-truncated', 'err-truncated')}
    if type(status.get('exitcode')) is not int or status['exitcode'] != 0 or status.get('err-data') or status.get('out-truncated') or status.get('err-truncated'):
        raise TerminalFailure('guest operation failed', metadata)
    output = status.get('out-data')
    if not isinstance(output, str) or len(output.encode()) > 180000:
        raise TerminalFailure('invalid output', metadata)
    try:
        result = json.loads(output)
    except (TypeError, ValueError) as error:
        raise TerminalFailure('invalid guest result', metadata) from error
    if not isinstance(result, dict):
        raise TerminalFailure('invalid guest result', metadata)
    return result
