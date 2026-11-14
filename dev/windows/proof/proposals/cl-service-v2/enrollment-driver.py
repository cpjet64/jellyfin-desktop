"""Fixed production enrollment; its v2 admission gate remains closed."""
import importlib.util
import sys

BOOTSTRAP = ('E:/CursorAI/JellyfinDesktop/deps/worktrees/compiler-contract-v2-review-20261114/'
             'dev/windows/proof/proposals/cl-service-v2/enrollment_bootstrap.py')


def main():
    if len(sys.argv) != 3:
        raise ValueError('endpoint and reviewed source digest required')
    spec = importlib.util.spec_from_file_location('fixed_enrollment_bootstrap', BOOTSTRAP)
    bootstrap = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bootstrap)
    bootstrap.setup('pycache-driver')
    profile, environment = bootstrap.profile(sys.argv[1], sys.argv[2])
    from build_only_v2.supervisor import production
    bootstrap.verify_origins()
    production(profile, environment, bootstrap.ROOT + '/build.receipt')


if __name__ == '__main__':
    main()
