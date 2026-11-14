"""Validate the settled fixed enrollment receipt; never launch a compiler."""
import ctypes as C
import importlib.util
import json
import sys

PROOF = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-fixture-detached-20261114/dev/windows/proof'
BASE = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-build-policy-20261113/dev/windows/proof/proposals/cl-cleanup-claim-v1'
RECEIPT = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-build-policy-20261113/deps/enrollment-cl-claim-20261114-01/build.receipt'
SOURCE = '80b842ae0d7ad4c3384b9eacfb9c492962febebae449c90a2b18d6b1e4f25915'


def read_closed_receipt(api, w):
    raw_handle = api.k.CreateFileW(RECEIPT, w.GENERIC_READ, 0, None, w.OPEN_EXISTING,
                                   w.FILE_FLAG_OPEN_REPARSE_POINT, None)
    handle = w.Handle(api, raw_handle)
    try:
        info = w.FILE_INFO()
        api.check(api.k.GetFileInformationByHandle(handle.value, C.byref(info)))
        if info.attributes & w.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError('receipt is a reparse point')
        data = bytearray()
        buffer = C.create_string_buffer(65536)
        while True:
            count = w.DWORD()
            api.check(api.k.ReadFile(handle.value, buffer, len(buffer), C.byref(count), None))
            if not count.value:
                break
            data.extend(buffer.raw[:count.value])
            if len(data) > 1048576:
                raise ValueError('receipt exceeds bound')
    finally:
        try:
            handle.close()
        except Exception as error:
            raise RuntimeError('exclusive receipt handle close failed') from error
    if api.opened != 1 or api.closed != 1 or api.close_failed:
        raise RuntimeError('receipt handle did not close')
    return bytes(data)


def failure_details(error):
    chain = []
    seen = set()
    while error is not None and id(error) not in seen and len(chain) < 8:
        seen.add(id(error))
        message = ''.join(c if 32 <= ord(c) < 127 else '?' for c in str(error))[:256]
        entry = {'type': type(error).__name__[:80], 'message': message}
        for field in ('hresult', 'winerror', 'errno'):
            value = getattr(error, field, None)
            entry[field] = value if type(value) is int else None
        chain.append(entry)
        error = error.__cause__ or (None if error.__suppress_context__ else error.__context__)
    return {'schema': 'enrollment-postcheck-error-v1', 'result': 'NONPASS', 'causes': chain}


def main():
    if sys.platform != 'win32' or len(sys.argv) != 2:
        raise ValueError('Windows and one observed exit required')
    arg = sys.argv[1]
    if not arg.isascii() or not arg.isdecimal() or len(arg) > 10 or (len(arg) > 1 and arg[0] == '0'):
        raise ValueError('invalid observed exit')
    observed_exit = int(arg)
    if observed_exit > 0xffffffff:
        raise ValueError('observed exit out of range')
    sys.path.insert(0, PROOF)
    from build_only import winapi as w
    from build_only.policy import PRODUCTION_POLICIES
    from build_only.receipt import validate_receipt
    spec = importlib.util.spec_from_file_location('enrollment_policy', BASE + '/enrollment_policy.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    policy = module.build_policy()
    if PRODUCTION_POLICIES:
        raise RuntimeError('production registry must start empty')
    PRODUCTION_POLICIES[policy.identity] = policy
    api = w.WinAPI()
    raw = read_closed_receipt(api, w)
    record = validate_receipt(raw, expected_source=SOURCE, expected_policy=policy,
                              observed_exit=observed_exit, observed_receipt_closed=True, production=True)
    result = {'schema': 'enrollment-postcheck-v1', 'result': 'VALIDATED', 'outputs': record['outputs'],
              'receipt_handles': {'opened': api.opened, 'closed': api.closed, 'failed': api.close_failed}}
    print(json.dumps(result, separators=(',', ':')))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(json.dumps(failure_details(error), separators=(',', ':')), file=sys.stderr)
        raise SystemExit(1)
