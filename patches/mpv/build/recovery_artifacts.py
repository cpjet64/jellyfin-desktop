"""Produce or verify a custom mpv manifest after a reviewed lock exists.

This does not run a build. Verification uses an explicitly selected dumpbin.
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from recovery_lock_preflight import parse_object, verify

HEX = re.compile(r"[0-9a-f]{64}\Z")
EXPORT = re.compile(r"^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(mpv_[A-Za-z0-9_]+)\b", re.M)
IMPORT = re.compile(r"(?<![A-Za-z0-9_])(?:__imp_)?(mpv_[A-Za-z0-9_]+)\b")
DEPENDENCY = re.compile(r"^\s+([^\s\\/]+\.dll)\s*$", re.I | re.M)


def hash_file(path):
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(block)
    return sha.hexdigest()


def contained_file(root, relative):
    if not isinstance(relative, str) or not relative or "\\" in relative or ":" in relative:
        raise ValueError("invalid artifact path")
    parts = relative.split("/")
    if any(part in ("", ".", "..") for part in parts):
        raise ValueError(f"invalid artifact path: {relative}")
    path = root
    for part in parts:
        path = path / part
        info = path.lstat()
        if path.is_symlink() or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError(f"reparse point in artifact path: {relative}")
    path.resolve(strict=True).relative_to(root.resolve(strict=True))
    if not path.is_file():
        raise ValueError(f"artifact is not a file: {relative}")
    return path


def check_root(root):
    current = root.absolute()
    while True:
        info = current.lstat()
        if current.is_symlink() or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("reparse point in artifact root")
        if current.parent == current:
            break
        current = current.parent


def tree_files(root, relative):
    directory = root / relative
    if not directory.is_dir():
        raise ValueError(f"Qt kit directory missing: {relative}")
    info = directory.lstat()
    if directory.is_symlink() or getattr(info, "st_file_attributes", 0) & 0x400:
        raise ValueError("reparse point at Qt kit inventory root")
    files = set()
    for base, directories, names in os.walk(directory, followlinks=False):
        for name in directories + names:
            item = Path(base) / name
            info = item.lstat()
            if item.is_symlink() or getattr(info, "st_file_attributes", 0) & 0x400:
                raise ValueError("reparse point in Qt kit inventory")
        for name in names:
            files.add((Path(base) / name).relative_to(root).as_posix())
    return files


def qt_manifest(root, lock):
    check_root(root)
    config = lock["qt_kit"]
    files = config.get("artifacts")
    if not isinstance(files, list) or not files or any(not isinstance(x, str) for x in files):
        raise ValueError("Qt kit artifact inventory missing")
    if len(files) != len({path.casefold() for path in files}):
        raise ValueError("duplicate Qt kit artifact path")
    required = {"lib/cmake/Qt6/Qt6Config.cmake", "plugins/platforms/qwindows.dll"}
    for module in ("Core", "Gui", "Quick", "OpenGL"):
        required.update((f"bin/Qt6{module}.dll", f"lib/Qt6{module}.lib"))
    required.update(tree_files(root, "include"))
    required.update(tree_files(root, "lib/cmake"))
    if not required.issubset(files):
        raise ValueError("Qt kit inventory omits required binary, header or CMake package files")
    configuration = config["configuration_file"]
    if configuration not in files or artifact(root, configuration)["sha256"] != config["configuration_sha256"]:
        raise ValueError("Qt kit configuration file differs from approved identity")
    return {"version": config["version"], "configuration_sha256": config["configuration_sha256"],
            "configuration_file": artifact(root, configuration),
            "source_patches": config["source_patches"],
            "artifacts": [artifact(root, path) for path in sorted(files)]}


def artifact(root, relative):
    path = contained_file(root, relative)
    size = path.stat().st_size
    if size <= 0:
        raise ValueError(f"empty artifact: {relative}")
    return {"path": relative, "sha256": hash_file(path), "size": size}


def checked_paths(config):
    required = ("dll", "msvc_import_library", "headers", "dependency_closure")
    if any(name not in config for name in required):
        raise ValueError("variant artifact specification is incomplete")
    if not isinstance(config["headers"], list) or not config["headers"]:
        raise ValueError("variant headers missing")
    if not isinstance(config["dependency_closure"], list):
        raise ValueError("variant dependency closure missing")
    all_paths = [config["dll"], config["msvc_import_library"]] + config["headers"] + config["dependency_closure"]
    if any(not isinstance(path, str) for path in all_paths) or len(all_paths) != len({path.casefold() for path in all_paths}):
        raise ValueError("duplicate variant artifact paths")
    return all_paths


def variant_manifest(root, name, lock):
    config = lock["variants"][name]
    checked_paths(config)
    prefix = "mpv-fallback" if name == "x86_64" else "mpv"
    if config["dll"] != f"{prefix}/libmpv-2.dll" or config["msvc_import_library"] != f"{prefix}/libmpv-2.dll.lib":
        raise ValueError(f"unexpected installed DLL or import library path: {name}")
    required_headers = {f"{prefix}/include/mpv/client.h", f"{prefix}/include/mpv/render.h"}
    if not required_headers.issubset(config["headers"]):
        raise ValueError(f"required installed headers missing: {name}")
    if any(not path.startswith(f"{prefix}/include/") for path in config["headers"]):
        raise ValueError(f"header outside variant include directory: {name}")
    if any(not path.startswith(f"{prefix}/") for path in config["dependency_closure"]):
        raise ValueError(f"dependency outside variant directory: {name}")
    if not HEX.fullmatch(config.get("configuration_sha256", "")):
        raise ValueError(f"configuration identity missing: {name}")
    system = config.get("system_imports")
    if not isinstance(system, list) or any(not isinstance(x, str) or not re.fullmatch(r"[A-Za-z0-9_.-]+\.dll", x, re.I) for x in system):
        raise ValueError(f"system import policy missing: {name}")
    if len(system) != len({value.casefold() for value in system}):
        raise ValueError(f"duplicate system import: {name}")
    return {
        "cpu_baseline": config["gcc_arch"], "target_triple": config["target"],
        "crt": config["crt"], "configuration_sha256": config["configuration_sha256"],
        "dll": artifact(root, config["dll"]),
        "msvc_import_library": artifact(root, config["msvc_import_library"]),
        "headers": [artifact(root, path) for path in config["headers"]],
        "dependency_closure": [artifact(root, path) for path in config["dependency_closure"]],
        "system_imports": sorted(system),
        "public_exports": sorted(lock["required_public_exports"]),
        "private_exports": sorted(lock["required_private_exports"]),
        "features": sorted(lock["required_features"]),
    }


def build_manifest(root, qt_root, lock, lock_sha):
    producer = lock.get("producer", {})
    for field in ("toolchain_lock_sha256", "host_tool_lock_sha256", "build_configuration_sha256"):
        if not HEX.fullmatch(producer.get(field, "")):
            raise ValueError(f"producer identity missing: {field}")
    if not re.fullmatch(r"[0-9a-f]{40}", producer.get("source_commit", "")):
        raise ValueError("producer source commit missing")
    return {"schema_version": 1, "input_lock_sha256": lock_sha,
            "provider_commit": lock["provider_commit"], "mpv_commit": lock["mpv_commit"],
            "bridge_patches": lock["bridge_patches"], "producer": producer,
            "qt_kit": qt_manifest(qt_root, lock),
            "variants": {name: variant_manifest(root, name, lock) for name in ("x86_64", "x86_64_v3")}}


def dumpbin(tool, option, file):
    creation = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    completed = subprocess.run([str(tool), option, str(file)], stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               timeout=30, check=False, creationflags=creation)
    if completed.returncode:
        raise ValueError(f"dumpbin {option} failed for {file.name}")
    return completed.stdout.decode("utf-8", errors="replace")


def verify_binary(root, tool, config, lock):
    dll = contained_file(root, config["dll"])
    library = contained_file(root, config["msvc_import_library"])
    for file in (dll, library):
        machines = re.findall(r"(?im)^\s*([0-9a-f]+)\s+machine\s*\([^)]+\)", dumpbin(tool, "/headers", file))
        if not machines or any(value.lower() != "8664" for value in machines):
            raise ValueError(f"unverified AMD64 machine: {file.name}")
    exported = set(EXPORT.findall(dumpbin(tool, "/exports", dll)))
    imported = set(IMPORT.findall(dumpbin(tool, "/linkermember:1", library)))
    required = set(lock["required_public_exports"]) | set(lock["required_private_exports"])
    if not required <= exported or not set(lock["required_public_exports"]) <= imported:
        raise ValueError(f"required DLL export or import-library symbol missing: {dll.name}")
    if imported - exported:
        raise ValueError(f"import library contains symbols absent from DLL: {library.name}")
    return exported


def verify_dependencies(root, tool, config):
    files = [config["dll"]] + config["dependency_closure"]
    paths = [contained_file(root, path) for path in files]
    names = [path.name.lower() for path in paths]
    if len(names) != len(set(names)):
        raise ValueError("ambiguous runtime dependency basenames")
    allowed = set(names) | {name.lower() for name in config["system_imports"]}
    for path in paths:
        if path.suffix.lower() != ".dll":
            raise ValueError(f"runtime dependency is not a DLL: {path.name}")
        imports = {name.lower() for name in DEPENDENCY.findall(dumpbin(tool, "/dependents", path))}
        if not imports:
            raise ValueError(f"dependency list unavailable: {path.name}")
        if imports - allowed:
            raise ValueError(f"unaccounted DLL dependencies for {path.name}: {sorted(imports - allowed)}")


def verify_windows(root, tool, lock):
    if os.name != "nt" or not tool.is_absolute() or not tool.is_file():
        raise ValueError("an absolute Windows dumpbin path is required for artifact verification")
    expected = lock.get("windows_dumpbin_sha256", "")
    if not HEX.fullmatch(expected) or hash_file(tool) != expected:
        raise ValueError("dumpbin identity differs from approved lock")
    for name in ("x86_64", "x86_64_v3"):
        config = lock["variants"][name]
        verify_binary(root, tool, config, lock)
        verify_dependencies(root, tool, config)


def verify_packaged(package_root, manifest):
    check_root(package_root)
    for name, installed in (("x86_64_v3", "libmpv-2.dll"), ("x86_64", "libmpv-fallback.dll")):
        expected = manifest["variants"][name]["dll"]
        actual = artifact(package_root, installed)
        if actual["sha256"] != expected["sha256"] or actual["size"] != expected["size"]:
            raise ValueError(f"packaged DLL differs from approved artifact: {installed}")
    qt = {item["path"]: item for item in manifest["qt_kit"]["artifacts"]}
    for source, installed in [(f"bin/Qt6{name}.dll", f"Qt6{name}.dll") for name in ("Core", "Gui", "Quick", "OpenGL")] + [("plugins/platforms/qwindows.dll", "platforms/qwindows.dll")]:
        expected = qt[source]
        actual = artifact(package_root, installed)
        if actual["sha256"] != expected["sha256"] or actual["size"] != expected["size"]:
            raise ValueError(f"packaged Qt artifact differs from approved kit: {installed}")


def verify_configured(args):
    values = [args.configured_mpv_library, args.configured_mpv_include, args.qt_config]
    if not any(value is not None for value in values) and not args.qt_artifact:
        return
    if not all(value is not None for value in values) or len(args.qt_artifact) != 8:
        raise ValueError("incomplete configured Qt/mpv target identities")
    pairs = [(args.configured_mpv_library, args.root / "mpv/libmpv-2.dll.lib"),
             (args.configured_mpv_include, args.root / "mpv/include"),
             (args.qt_config, args.qt_root / "lib/cmake/Qt6/Qt6Config.cmake")]
    for actual, expected in pairs:
        if not os.path.samefile(actual, expected):
            raise ValueError("configured Qt/mpv path differs from verified kit")
    expected_qt = [args.qt_root / f"{directory}/Qt6{module}.{extension}"
                   for module in ("Core", "Gui", "Quick", "OpenGL")
                   for directory, extension in (("bin", "dll"), ("lib", "lib"))]
    for expected in expected_qt:
        if sum(os.path.samefile(actual, expected) for actual in args.qt_artifact) != 1:
            raise ValueError("configured Qt imported target differs from verified kit")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("produce", "verify"))
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--approved-lock-sha256", required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--qt-root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--approved-manifest-sha256")
    parser.add_argument("--dumpbin", type=Path)
    parser.add_argument("--packaged", type=Path)
    parser.add_argument("--configured-mpv-library", type=Path)
    parser.add_argument("--configured-mpv-include", type=Path)
    parser.add_argument("--qt-config", type=Path)
    parser.add_argument("--qt-artifact", type=Path, action="append", default=[])
    args = parser.parse_args()
    try:
        lock = verify(args.lock, args.approved_lock_sha256)
        check_root(args.root)
        expected = build_manifest(args.root, args.qt_root, lock, args.approved_lock_sha256)
        if args.mode == "produce":
            with args.manifest.open("x", encoding="utf-8", newline="\n") as stream:
                json.dump(expected, stream, indent=2, sort_keys=True)
                stream.write("\n")
            print("Manifest created from actual file hashes; Windows artifact verification remains required.")
        else:
            if not args.approved_manifest_sha256 or not HEX.fullmatch(args.approved_manifest_sha256):
                raise ValueError("approved manifest digest missing")
            payload = args.manifest.read_bytes()
            if hashlib.sha256(payload).hexdigest() != args.approved_manifest_sha256:
                raise ValueError("manifest digest differs from approved artifact")
            if parse_object(payload) != expected:
                raise ValueError("manifest differs from approved lock or staged files")
            if args.dumpbin is None:
                raise ValueError("dumpbin path missing")
            verify_windows(args.root, args.dumpbin, lock)
            verify_configured(args)
            if build_manifest(args.root, args.qt_root, lock, args.approved_lock_sha256) != expected:
                raise ValueError("staged artifacts changed during verification")
            if args.packaged is not None:
                verify_packaged(args.packaged, expected)
            print("Custom mpv artifact hashes, exports and dependencies checked; runtime acceptance is separate.")
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print(f"Custom mpv artifact operation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
