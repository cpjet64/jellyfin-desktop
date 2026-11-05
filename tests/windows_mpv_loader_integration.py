"""Run in an x64 MSVC environment after the coordinator releases the execution hold.

The fixed fixture sources emit a finite set of markers. subprocess.run captures
output in memory without a hard cap; only retained timeout evidence is capped.
"""
import hashlib
import csv
import os
import re
from pathlib import Path
import shutil
import stat
import subprocess

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "windows-mpv-loader-test"
INPUT_SOURCES = (ROOT / "src" / "player" / "WindowsMpvLoader.cpp",
                 ROOT / "src" / "player" / "WindowsLoaderMode.h",
                 ROOT / "tests" / "windows_mpv_delay_test.cpp",
                 ROOT / "tests" / "windows_mpv_stub.cpp",
                 ROOT / "tests" / "windows_mpv_dependency.cpp",
                 ROOT / "tests" / "windows_mpv_loader_integration.py")


def run(args, expected=0, env=None):
    if Path(args[0]).name in ("primary.exe", "fallback.exe"):
        fixture_identity(Path(args[0]))
    try:
        result = subprocess.run(args, cwd=OUT, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=30, env=env,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    except subprocess.TimeoutExpired as error:
        partial = error.stdout or b""
        if isinstance(partial, str):
            partial = partial.encode("utf-8", errors="replace")
        (OUT / "loader-timeout.txt").write_bytes(partial[:65536])
        (OUT / "loader-timeout-pending").touch()
        raise RuntimeError("Fixture timeout; child state at timeout is not established; "
                           f"bounded partial output retained at {OUT}") from error
    if Path(args[0]).name in ("primary.exe", "fallback.exe"):
        print(result.stdout, end="", flush=True)
    if result.returncode != expected:
        raise RuntimeError(f"{args}: exit {result.returncode}, expected {expected}\n{result.stdout}")
    if Path(args[0]).name in ("primary.exe", "fallback.exe") and expected in (11, 22):
        verify_mode_trace(result.stdout)
    return result.stdout


def verify_mode_trace(output):
    before = re.search(r'fixture before thread=(\d+) process=(\d+)', output)
    inside = re.search(r'before load mode=(\d+)', output)
    after = re.search(r'fixture after thread=(\d+) process=(\d+)', output)
    if not before or not inside or not after:
        raise RuntimeError('Fixture mode trace is incomplete')
    thread, process = map(int, before.groups())
    if not thread or int(inside.group(1)) != (thread | 1) or tuple(map(int, after.groups())) != (thread, process):
        raise RuntimeError('Fixture mode trace does not prove exact restoration')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fixture_identity(executable):
    """Print prelaunch identities for the fixed sources and current case inputs."""
    revision = run(["git", "rev-parse", "HEAD"]).strip()
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise RuntimeError("Unable to identify source revision")
    print(f"fixture source_revision={revision} case={executable.name} executable_sha256={digest(executable)}",
          flush=True)
    for source in INPUT_SOURCES:
        print(f"fixture source={source.name} sha256={digest(source)}", flush=True)
    for path in (executable.parent / "libmpv-2.dll", executable.parent / "libmpv-fallback.dll",
                 OUT / "path-only" / "libmpv-2.dll"):
        print(f"fixture input={path.name} scope={'path-only' if path.parent.name == 'path-only' else 'adjacent'} "
              f"sha256={digest(path) if path.is_file() else 'missing'}", flush=True)


def expect_denied(operation, description):
    try:
        operation()
    except PermissionError:
        return
    raise RuntimeError(f"Fixture unexpectedly permits {description}")


def verify_protected_install():
    """Exercise the loader in a fixture that the current token cannot modify."""
    protected = OUT / "protected-install"
    protected.mkdir()
    for name in ("primary.exe", "fallback.exe", "libmpv-2.dll", "libmpv-fallback.dll"):
        shutil.copy2(OUT / name, protected / name)
    sid = next(csv.reader([run(["whoami", "/user", "/fo", "csv", "/nh"]).strip()]))[-1]
    if not sid.startswith("S-1-"):
        raise RuntimeError("Could not determine current user SID")
    acl_file = OUT / "protected-install.acl"
    restored_acl = OUT / "protected-install-restored.acl"
    pending = OUT / "acl-restore-pending"
    run(["icacls", str(protected), "/save", str(acl_file), "/t"])
    pending.touch()
    try:
        run(["icacls", str(protected), "/inheritance:r", "/grant:r", f"*{sid}:(OI)(CI)(RX)"])
        primary = protected / "libmpv-2.dll"
        fallback = protected / "libmpv-fallback.dll"
        hashes = (digest(primary), digest(fallback))
        replacement = OUT / "replacement.dll"
        shutil.copy2(primary, replacement)
        expect_denied(lambda: (protected / "new-file").write_bytes(b"probe"), "file creation")
        expect_denied(lambda: os.replace(replacement, primary), "file replacement")
        expect_denied(lambda: fallback.unlink(), "file deletion")
        for _ in range(2):
            run([str(protected / "primary.exe")], 11)
            run([str(protected / "fallback.exe")], 22)
        if (digest(primary), digest(fallback)) != hashes:
            raise RuntimeError("Protected installation DLL contents changed")
    finally:
        # This freshly created tree inherits its ACLs. /restore requires a
        # privilege unavailable to a standard user; reset restores inheritance.
        run(["icacls", str(protected), "/reset", "/t"])
        run(["icacls", str(protected), "/save", str(restored_acl), "/t"])
        if acl_file.read_bytes() != restored_acl.read_bytes():
            raise RuntimeError(f"Fixture ACL differs after reset; retained at {OUT}")
        pending.unlink()


def compile_fixtures():
    source = ROOT / "tests" / "windows_mpv_stub.cpp"
    common = ["cl", "/nologo", "/LD", "/W4", "/WX", str(source)]
    run(common + ["/DMPV_STUB_VALUE=11", "/Fe:libmpv-2.dll",
                  "/link", "/IMPLIB:mpv-fixture.lib"])
    run(common + ["/DMPV_STUB_VALUE=22", "/Fe:libmpv-fallback.dll"])
    sources = [str(ROOT / "tests" / "windows_mpv_delay_test.cpp"),
               str(ROOT / "src" / "player" / "WindowsMpvLoader.cpp")]
    for value, exe in [(1, "primary.exe"), (0, "fallback.exe")]:
        run(["cl", "/nologo", "/std:c++14", "/EHsc", "/W4", "/WX",
             f"/DJELLYFIN_LOADER_TEST_V3={value}", *sources, f"/Fe:{exe}",
             "/link", "mpv-fixture.lib", "delayimp.lib", "user32.lib",
             "/DELAYLOAD:libmpv-2.dll"])


def verify():
    primary = OUT / "libmpv-2.dll"
    fallback = OUT / "libmpv-fallback.dll"
    before = (digest(primary), digest(fallback))
    imports = run(["dumpbin", "/imports", "fallback.exe"])
    if "libmpv-2.dll" not in imports or "delay load imports" not in imports.lower():
        raise RuntimeError("PE does not list libmpv as a delay import:\n" + imports)
    verify_protected_install()
    for path in (primary, fallback):
        os.chmod(path, stat.S_IREAD)
    for _ in range(2):
        run([str(OUT / "primary.exe")], 11)
        run([str(OUT / "fallback.exe")], 22)
    if (digest(primary), digest(fallback)) != before:
        raise RuntimeError("DLL contents changed during loading")
    for path in (primary, fallback):
        os.chmod(path, stat.S_IWRITE | stat.S_IREAD)
    fallback.unlink()
    run([str(OUT / "fallback.exe")], 126)
    fallback.write_bytes(b"not a valid DLL")
    corrupt_hash = digest(fallback)
    run([str(OUT / "fallback.exe")], 126)
    if digest(primary) != before[0] or digest(fallback) != corrupt_hash:
        raise RuntimeError("DLL contents changed during failure")
    verify_incompatible(primary, fallback, before[0])
    print("Delay import, ACL-protected loads, failure cases, and DLL hashes passed.")


def verify_incompatible(primary, fallback, primary_hash):
    stub = ROOT / "tests" / "windows_mpv_stub.cpp"
    fallback.unlink()
    run(["cl", "/nologo", "/LD", "/DMPV_STUB_VALUE=22",
         "/DMPV_STUB_MISSING_EXPORT", str(stub), "/Fe:libmpv-fallback.dll"])
    fallback_hash = digest(fallback)
    run([str(OUT / "fallback.exe")], 127)
    if digest(fallback) != fallback_hash:
        raise RuntimeError("Incompatible fallback changed")
    fallback.unlink()
    dependency = ROOT / "tests" / "windows_mpv_dependency.cpp"
    run(["cl", "/nologo", "/LD", str(dependency), "/Fe:fixture-dependency.dll"])
    run(["cl", "/nologo", "/LD", "/DMPV_STUB_VALUE=22", "/DMPV_STUB_DEPENDENCY",
         str(stub), "/Fe:libmpv-fallback.dll", "/link", "fixture-dependency.lib"])
    fallback_hash = digest(fallback)
    (OUT / "fixture-dependency.dll").unlink()
    run([str(OUT / "fallback.exe")], 126)
    if digest(primary) != primary_hash or digest(fallback) != fallback_hash:
        raise RuntimeError("DLL contents changed during dependency failure")
    path_dir = OUT / "path-only"
    path_dir.mkdir()
    shutil.copy2(primary, path_dir / primary.name)
    primary.unlink()
    env = os.environ.copy()
    env["PATH"] = str(path_dir) + os.pathsep + env.get("PATH", "")
    run([str(OUT / "primary.exe")], 126, env=env)
    if digest(path_dir / primary.name) != primary_hash:
        raise RuntimeError("PATH-only DLL changed")


def main():
    if OUT.exists():
        raise RuntimeError(f"Refusing to overwrite existing directory: {OUT}")
    OUT.mkdir(parents=True)
    try:
        compile_fixtures()
        verify()
    finally:
        if (OUT / "acl-restore-pending").exists() or (OUT / "loader-timeout-pending").exists():
            print(f"Fixture retained for recovery or timeout inspection: {OUT}")
        else:
            for path in OUT.iterdir():
                if path.is_file():
                    os.chmod(path, stat.S_IWRITE | stat.S_IREAD)
            shutil.rmtree(OUT)


if __name__ == "__main__":
    main()
