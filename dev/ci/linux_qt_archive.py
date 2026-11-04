"""Job-local, bounded Linux Qt acquisition and verified publication."""

from contextlib import ExitStack
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys

from linux_qt_library import load_library
from linux_qt_plan import MAX_EXPANSION, attach_replays, scan_archives
from linux_qt_reader import Reader
from qt_sdk_archive import check_storage, download_archive, stage_entries


BUILD_RESERVE = 8 * 1024 * 1024 * 1024
DEADLINE_SECONDS = 1800


@dataclass(frozen=True)
class Candidate:
    workspace: Path
    prefix: Path
    manifest_sha256: str


def manifest_hash():
    return hashlib.sha256(Path(__file__).with_name("qt-6.9.3-unix.json").read_bytes()).hexdigest()


def run_worker(workspace):
    """Run decoding and probes in a bounded, task-owned process group."""
    args = [sys.executable, str(Path(__file__).resolve()), "--worker", str(workspace)]
    child = subprocess.Popen(args, start_new_session=True)
    try:
        result = child.wait(timeout=DEADLINE_SECONDS)
    except subprocess.TimeoutExpired as error:
        raise TimeoutError("Qt SDK acquisition deadline exceeded") from error
    finally:
        active_error = sys.exc_info()[0] is not None
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        except OSError:
            if not active_error:
                raise
        if child.poll() is None:
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                if not active_error:
                    raise RuntimeError("Qt worker did not terminate")
    if result:
        raise RuntimeError(f"Qt SDK acquisition failed: worker exit {result}")


def stage_qt(workspace):
    if sys.platform != "linux":
        raise ValueError("Qt SDK staging requires Linux")
    workspace = Path(workspace).resolve(strict=True)
    run_worker(workspace)
    prefix = workspace / "qt-candidate" / "6.9.3/gcc_64"
    if not prefix.is_dir():
        raise ValueError("Qt worker did not create a complete candidate")
    return Candidate(workspace, prefix.resolve(strict=True), manifest_hash())


def prepare(workspace):
    manifest = json.loads(Path(__file__).with_name("qt-6.9.3-unix.json").read_text(encoding="utf-8"))
    platform = manifest["platforms"]["linux"]
    from qt_sdk_archive import validate_manifest
    items = validate_manifest(platform)
    check_storage(workspace, platform["archive_bytes_total"], MAX_EXPANSION, BUILD_RESERVE)
    library = load_library()
    downloads = workspace / "qt-archives"
    downloads.mkdir(exist_ok=False)
    archives = []
    for index, item in enumerate(items):
        path = downloads / f"{index:02d}.7z"
        download_archive(item, path)
        archives.append((item, path))
    root = platform["target_subdirectory"]
    groups = scan_archives(archives, library, root)
    with ExitStack() as stack:
        readers = [stack.enter_context(Reader(path, library, item)) for item, path in archives]
        replay_groups, replays = attach_replays(groups, readers)

        def verify(stage):
            for replay in replays:
                replay.finish()
            stack.close()

        stage_entries(replay_groups, workspace / "qt-stage", workspace / "qt-candidate",
                      MAX_EXPANSION, root, verify)


def complete_qt(candidate, report):
    if not isinstance(candidate, Candidate) or candidate.manifest_sha256 != manifest_hash():
        raise ValueError("invalid Qt SDK candidate identity")
    prefix = candidate.workspace / "qt-candidate" / "6.9.3/gcc_64"
    if prefix.resolve(strict=True) != candidate.prefix:
        raise ValueError("Qt SDK candidate path changed")
    expected = {"PKG_CONFIG_PATH": str(prefix / "lib/pkgconfig"), "LD_LIBRARY_PATH": str(prefix / "lib")}
    if not isinstance(report, dict) or report.get("environment") != expected:
        raise ValueError("Qt SDK probe did not validate the candidate")
    receipt = candidate.workspace / "qt-sdk-ready.json"
    temporary = candidate.workspace / "qt-sdk-ready.pending"
    payload = json.dumps({"prefix": str(prefix), "manifest_sha256": candidate.manifest_sha256}, sort_keys=True)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    with os.fdopen(os.open(temporary, flags, 0o600), "w", encoding="utf-8") as output:
        output.write(payload + "\n")
        output.flush()
        os.fsync(output.fileno())
    os.link(temporary, receipt, follow_symlinks=False)
    temporary.unlink()
    return prefix


def worker_main():
    import resource

    resource.setrlimit(resource.RLIMIT_AS, (6 * 1024**3, 6 * 1024**3))
    resource.setrlimit(resource.RLIMIT_CPU, (1500, 1500))
    if len(sys.argv) != 3 or sys.argv[1] != "--worker":
        raise ValueError("invalid Qt worker invocation")
    workspace = Path(sys.argv[2]).resolve(strict=True)
    prepare(workspace)


if __name__ == "__main__":
    worker_main()
