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
import time

from linux_qt_library import load_library
from linux_qt_plan import MAX_EXPANSION, attach_replays, scan_archives
from linux_qt_reader import Reader
from qt_sdk_archive import check_storage, download_archive, stage_entries


BUILD_RESERVE = 8 * 1024 * 1024 * 1024
DEADLINE_SECONDS = 1800
ADDRESS_SPACE_BYTES = 6 * 1024**3
CPU_SECONDS = 1500
GROUP_EXIT_SECONDS = 5


def decoder_limits():
    return {"address_space_bytes": ADDRESS_SPACE_BYTES, "cpu_seconds": CPU_SECONDS,
            "wall_seconds": DEADLINE_SECONDS}


@dataclass(frozen=True)
class Candidate:
    workspace: Path
    prefix: Path
    manifest_sha256: str
    archive_input_sha256: str = ""


def manifest_hash():
    return hashlib.sha256(Path(__file__).with_name("qt-6.9.3-unix.json").read_bytes()).hexdigest()


def settle_group(child):
    """Kill only the launched process group and prove its disappearance."""
    try:
        os.killpg(child.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    try:
        child.wait(timeout=GROUP_EXIT_SECONDS)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("Qt worker did not exit after termination") from error
    deadline = time.monotonic() + GROUP_EXIT_SECONDS
    while True:
        try:
            os.killpg(child.pid, 0)
        except ProcessLookupError:
            return
        except OSError as error:
            raise RuntimeError("cannot verify Qt worker process-group exit") from error
        if time.monotonic() >= deadline:
            raise RuntimeError("Qt worker process group did not disappear")
        time.sleep(0.05)


def run_worker(workspace, archive_cache=None):
    """Run decoding and probes in a bounded, task-owned process group."""
    args = [sys.executable, str(Path(__file__).resolve()), "--worker", str(workspace)]
    if archive_cache is not None:
        if not isinstance(archive_cache, dict):
            raise ValueError("explicit Qt archive cache must be a mapping")
        args.extend(["--cache-map", json.dumps(archive_cache, sort_keys=True)])
    try:
        child = subprocess.Popen(args, start_new_session=True)
    except OSError as error:
        error.add_note(f"Qt decoder limits: {decoder_limits()}")
        raise
    failure = None
    try:
        result = child.wait(timeout=DEADLINE_SECONDS)
        if result:
            failure = RuntimeError(f"Qt SDK acquisition failed: worker exit {result}; limits={decoder_limits()}")
    except subprocess.TimeoutExpired:
        failure = TimeoutError(f"Qt SDK acquisition deadline exceeded; limits={decoder_limits()}")
    except BaseException as error:
        failure = error
        failure.add_note(f"Qt decoder limits: {decoder_limits()}")
    try:
        settle_group(child)
    except BaseException as error:
        if failure is None:
            raise RuntimeError(f"Qt worker cleanup failed; limits={decoder_limits()}") from error
        failure.add_note(f"Qt worker cleanup also failed: {error}")
    if failure is not None:
        raise failure


def stage_qt(workspace, archive_cache=None):
    if sys.platform != "linux":
        raise ValueError("Qt SDK staging requires Linux")
    from linux_qt_pc import ZLIB_EVIDENCE, host_zlib, normalize_qt_pc, validate_pkg_environment, write_exclusive
    workspace = Path(workspace).resolve(strict=True)
    prefix = workspace / "qt-candidate" / "6.9.3/gcc_64"
    validate_pkg_environment(prefix)
    baseline = host_zlib(prefix, include_candidate=False)
    if archive_cache is None:
        run_worker(workspace)
    else:
        run_worker(workspace, archive_cache)
    if not prefix.is_dir() or not (workspace / "qt-link-modes.json").is_file():
        raise ValueError("Qt worker did not create a complete candidate")
    normalize_qt_pc(prefix.resolve(strict=True), workspace)
    if host_zlib(prefix) != baseline:
        raise ValueError("host zlib metadata changed after Qt normalization")
    write_exclusive(workspace / ZLIB_EVIDENCE, json.dumps(baseline, sort_keys=True).encode("utf-8") + b"\n")
    archive_input = ""
    if archive_cache is not None:
        from linux_qt_cache import verify_evidence
        archive_input = verify_evidence(workspace, linux_items()[1], manifest_hash())["evidence_sha256"]
    return Candidate(workspace, prefix.resolve(strict=True), manifest_hash(), archive_input)


def linux_items():
    manifest = json.loads(Path(__file__).with_name("qt-6.9.3-unix.json").read_text(encoding="utf-8"))
    platform = manifest["platforms"]["linux"]
    from qt_sdk_archive import validate_manifest
    return platform, validate_manifest(platform)


def prepare(workspace, archive_cache=None):
    platform, items = linux_items()
    check_storage(workspace, platform["archive_bytes_total"], MAX_EXPANSION, BUILD_RESERVE)
    library = load_library()
    if archive_cache is None:
        downloads = workspace / "qt-archives"
        downloads.mkdir(exist_ok=False)
        archives = []
        for index, item in enumerate(items):
            path = downloads / f"{index:02d}.7z"
            download_archive(item, path)
            archives.append((item, path))
    else:
        from linux_qt_cache import copy_archives, verify_evidence
        archives = copy_archives(items, archive_cache, workspace, manifest_hash())
        verify_evidence(workspace, items, manifest_hash())
    root = platform["target_subdirectory"]
    groups = scan_archives(archives, library, root)
    with ExitStack() as stack:
        readers = [stack.enter_context(Reader(path, library, item)) for item, path in archives]
        replay_groups, replays = attach_replays(groups, readers)
        link_records = []

        def verify(stage):
            for replay in replays:
                replay.finish()
            stack.close()
            inventory = workspace / "qt-link-modes.json"
            with inventory.open("x", encoding="utf-8") as output:
                json.dump(link_records, output, ensure_ascii=True, sort_keys=True)
                output.write("\n")
                output.flush()
                os.fsync(output.fileno())

        stage_entries(replay_groups, workspace / "qt-stage", workspace / "qt-candidate",
                      MAX_EXPANSION, root, verify, link_records)


def complete_qt(candidate, report):
    from linux_qt_pc import ZLIB_EVIDENCE, host_zlib, read_evidence, verify_normalized
    if not isinstance(candidate, Candidate) or candidate.manifest_sha256 != manifest_hash():
        raise ValueError("invalid Qt SDK candidate identity")
    prefix = candidate.workspace / "qt-candidate" / "6.9.3/gcc_64"
    if prefix.resolve(strict=True) != candidate.prefix:
        raise ValueError("Qt SDK candidate path changed")
    expected = {"PKG_CONFIG_PATH": str(prefix / "lib/pkgconfig"), "LD_LIBRARY_PATH": str(prefix / "lib")}
    if not isinstance(report, dict) or report.get("environment") != expected:
        raise ValueError("Qt SDK probe did not validate the candidate")
    if (report.get("qt_prefix") != str(prefix) or not isinstance(report.get("mpv_include"), str) or
            not isinstance(report.get("mpv_library"), str)):
        raise ValueError("Qt SDK probe returned incomplete input identities")
    include, library = Path(report["mpv_include"]), Path(report["mpv_library"])
    if not include.is_absolute() or not include.is_relative_to(candidate.workspace) or not library.is_absolute():
        raise ValueError("Qt SDK probe returned invalid mpv input identities")
    normalization = verify_normalized(prefix, candidate.workspace)
    archive_input = None
    if candidate.archive_input_sha256:
        from linux_qt_cache import verify_evidence
        archive_input = verify_evidence(candidate.workspace, linux_items()[1], candidate.manifest_sha256)
        if archive_input["evidence_sha256"] != candidate.archive_input_sha256:
            raise ValueError("cached archive input evidence changed")
    zlib_evidence = read_evidence(candidate.workspace / ZLIB_EVIDENCE, 65536)
    if host_zlib(prefix) != json.loads(zlib_evidence):
        raise ValueError("host zlib metadata changed after SDK probe")
    inventory = (candidate.workspace / "qt-link-modes.json").read_bytes()
    links = json.loads(inventory)
    if not isinstance(links, list):
        raise ValueError("invalid Qt symlink mode inventory")
    receipt = candidate.workspace / "qt-sdk-ready.json"
    temporary = candidate.workspace / "qt-sdk-ready.pending"
    receipt_data = {"prefix": str(prefix), "manifest_sha256": candidate.manifest_sha256,
                          "decoder_limits": decoder_limits(), "symlink_count": len(links),
                          "symlink_inventory_sha256": hashlib.sha256(inventory).hexdigest(),
                          "qt_pc_normalization": normalization,
                          "host_zlib_sha256": hashlib.sha256(zlib_evidence).hexdigest()}
    if archive_input is not None:
        receipt_data["archive_input"] = archive_input
    payload = json.dumps(receipt_data, sort_keys=True)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    with os.fdopen(os.open(temporary, flags, 0o600), "w", encoding="utf-8") as output:
        output.write(payload + "\n")
        output.flush()
        os.fsync(output.fileno())
    if verify_normalized(prefix, candidate.workspace) != normalization:
        raise ValueError("Qt metadata normalization changed before READY")
    if read_evidence(candidate.workspace / ZLIB_EVIDENCE, 65536) != zlib_evidence or host_zlib(prefix) != json.loads(zlib_evidence):
        raise ValueError("host zlib metadata changed before READY")
    if archive_input is not None and verify_evidence(candidate.workspace, linux_items()[1], candidate.manifest_sha256) != archive_input:
        raise ValueError("cached archive copies changed before READY")
    os.link(temporary, receipt, follow_symlinks=False)
    temporary.unlink()
    return prefix


def worker_main():
    import resource

    resource.setrlimit(resource.RLIMIT_AS, (ADDRESS_SPACE_BYTES, ADDRESS_SPACE_BYTES))
    resource.setrlimit(resource.RLIMIT_CPU, (CPU_SECONDS, CPU_SECONDS))
    if len(sys.argv) not in (3, 5) or sys.argv[1] != "--worker" or (len(sys.argv) == 5 and sys.argv[3] != "--cache-map"):
        raise ValueError("invalid Qt worker invocation")
    workspace = Path(sys.argv[2]).resolve(strict=True)
    archive_cache = json.loads(sys.argv[4]) if len(sys.argv) == 5 else None
    if len(sys.argv) == 5 and not isinstance(archive_cache, dict):
        raise ValueError("explicit Qt archive cache must be a mapping")
    prepare(workspace, archive_cache)


if __name__ == "__main__":
    worker_main()
