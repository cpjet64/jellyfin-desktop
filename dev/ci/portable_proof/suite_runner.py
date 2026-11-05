"""Run one exact portable test module and report every skip and outcome."""
import importlib.util
import json
from pathlib import Path
import sys
import unittest


def allowed_skips(platform, module):
    if module == "test_evidence":
        return {"EvidenceTest.test_hidden_powershell_streams_stdout_stderr_and_exit"}
    if platform == "darwin" and module == "test_qt_sdk_archive":
        return {"QtArchiveTest.test_overlay_link_permissions_and_publication",
                "QtArchiveTest.test_real_link_materialization_preserves_target_mode",
                "QtArchiveTest.test_native_publication", "QtArchiveTest.test_native_publication_preserves_existing_destination"}
    return set()


def main():
    root, relative, kind = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
    if (kind == "macos" and sys.version_info[:3] != (3, 13, 15)) or (kind == "linux" and sys.version_info[:2] < (3, 10)):
        raise RuntimeError("unexpected native Python version")
    path = root / relative
    sys.path.insert(0, str(root / "dev/ci"))
    sys.path.insert(0, str(root / "tests"))
    spec = importlib.util.spec_from_file_location(path.stem, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromModule(module))
    skipped = sorted(test.id().split(".", 1)[1] for test, _ in result.skipped)
    allowed = allowed_skips(sys.platform, path.stem)
    summary = {"module": relative, "python": list(sys.version_info[:3]), "tests": result.testsRun,
               "failures": len(result.failures), "errors": len(result.errors), "skipped": skipped,
               "unexpected_successes": len(result.unexpectedSuccesses)}
    print("W411_RESULT=" + json.dumps(summary, sort_keys=True), flush=True)
    if not result.testsRun or not result.wasSuccessful() or set(skipped) != allowed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
