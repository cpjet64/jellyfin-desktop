"""Fixed independent production receipt postcheck; qualification remains closed."""
import importlib.util
import json
import sys

BOOTSTRAP = ('E:/CursorAI/JellyfinDesktop/deps/worktrees/compiler-contract-v2-review-20261114/'
             'dev/windows/proof/proposals/cl-service-v2/enrollment_bootstrap.py')


def main():
    if len(sys.argv) != 4 or not sys.argv[3].isascii() or not sys.argv[3].isdecimal():
        raise ValueError('endpoint, source digest and observed helper exit required')
    spec = importlib.util.spec_from_file_location('fixed_enrollment_bootstrap', BOOTSTRAP)
    bootstrap = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bootstrap)
    bootstrap.setup('pycache-postcheck')
    profile, _ = bootstrap.profile(sys.argv[1], sys.argv[2])
    from build_only_v2.postcheck import postcheck
    bootstrap.verify_origins()
    record = postcheck(receipt_path=bootstrap.ROOT + '/build.receipt',
                       outer_path=bootstrap.ROOT + '/outer.receipt', expected_profile=profile,
                       expected_source=sys.argv[2], observed_helper_exit=int(sys.argv[3]))
    print(json.dumps({'schema': 'enrollment-postcheck-v2', 'result': 'VALIDATED',
                      'outputs': record['outputs']}, separators=(',', ':')))


if __name__ == '__main__':
    main()
