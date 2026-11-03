"""Run from an x64 MSVC developer environment: python tests/windows_mpv_loader_integration.py."""
import hashlib
import os
from pathlib import Path
import shutil
import stat
import subprocess

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "windows-mpv-loader-test"


def run(args, expected=0):
    result = subprocess.run(args, cwd=OUT, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=30,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode != expected:
        raise RuntimeError(f"{args}: exit {result.returncode}, expected {expected}\n{result.stdout}")
    return result.stdout


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


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
    print("Delay import, repeated read-only loads, failure cases, and DLL hashes passed.")


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
    result = subprocess.run([str(OUT / "primary.exe")], cwd=OUT, env=env,
                            timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode != 126:
        raise RuntimeError("Missing primary did not fail safely with a PATH copy present")
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
        for path in OUT.iterdir():
            if path.is_file():
                os.chmod(path, stat.S_IWRITE | stat.S_IREAD)
        shutil.rmtree(OUT)


if __name__ == "__main__":
    main()
