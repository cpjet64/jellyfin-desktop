"""Run from an x64 MSVC developer environment: python tests/windows_mpv_loader_integration.py."""
import hashlib
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "windows-mpv-loader-test"


def run(args, expected=0):
    result = subprocess.run(args, cwd=OUT, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
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
    run([str(OUT / "primary.exe")], 11)
    run([str(OUT / "fallback.exe")], 22)
    if (digest(primary), digest(fallback)) != before:
        raise RuntimeError("DLL contents changed during loading")
    fallback.unlink()
    run([str(OUT / "fallback.exe")], 126)
    fallback.write_bytes(b"not a valid DLL")
    corrupt_hash = digest(fallback)
    run([str(OUT / "fallback.exe")], 126)
    if digest(primary) != before[0] or digest(fallback) != corrupt_hash:
        raise RuntimeError("DLL contents changed during failure")
    print("Delay import, both selections, missing/corrupt fallback, and DLL hashes passed.")


def main():
    if OUT.exists():
        raise RuntimeError(f"Refusing to overwrite existing directory: {OUT}")
    OUT.mkdir(parents=True)
    try:
        compile_fixtures()
        verify()
    finally:
        shutil.rmtree(OUT)


if __name__ == "__main__":
    main()
