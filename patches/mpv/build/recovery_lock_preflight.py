"""Fail-closed source-lock preflight. This does not validate build artifacts."""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "patches" / "mpv" / "build"
PROVIDER = "1f8d2e76255abc48f0456d9be99b4cab9c6566e6"
MPV = "01e2533f3143b8940857ca419b0b367f583e05ca"
RUST_MANIFEST = "b14ac4f9ad835f082cdffd0dc4797c202ae70094a09fa0a6a8100a56e2e17ed3"
PATCHES = {
    "0001-recovery-frame-observation.patch": "1b44233c501f7cbab66e64269f2d83f0f57b7bc5aceca8975cb218ce36b52edc",
    "0002-recovery-seek-identity.patch": "97150ebc3f75adad6c1a4591c0a1c1a168a82d7190cd5d59b03150bf17d292a7",
    "0003-recovery-seek-epoch-tests.patch": "6f6221c6aa1a23d8991324d7cad5748a1a0768ad8f318bffb360ca36f8975e7d",
}
PRIVATE_EXPORTS = {
    "mpv_render_context_render_recovery_v1", "mpv_render_context_render_recovery_v2",
    "mpv_recovery_seek_async_v1", "mpv_recovery_seek_cancel_v1",
}
QT_BRIDGE_PATHS = {
    "patches/qt/base/QtRecoverySwap.cpp",
    "patches/qt/base/qtbase-6.9.3-swap.patch",
    "patches/qt/base/source-identity.txt",
    "patches/qt/quick/QtRecoveryBatchUpload.cpp",
    "patches/qt/quick/QtRecoveryClipState_p.h",
    "patches/qt/quick/QtRecoveryDrawSet_p.h",
    "patches/qt/quick/QtRecoveryPrimitive.cpp",
    "patches/qt/quick/QtRecoveryPrimitive_p.h",
    "patches/qt/quick/QtRecoveryQuick.cpp",
    "patches/qt/quick/QtRecoveryQuick_p.h",
    "patches/qt/quick/QtRecoverySubmissionAbi.h",
    "patches/qt/quick/QtRecoveryUploadDebug.cpp",
    "patches/qt/quick/qtdeclarative-6.9.3-batch-renderer.patch",
    "patches/qt/quick/qtdeclarative-6.9.3-ticket-fbo.patch",
    "patches/qt/runtime/QtRecoveryAbi.h",
}
MINIMUM_PUBLIC_EXPORTS = {
    "mpv_command_async", "mpv_command_string", "mpv_create", "mpv_error_string", "mpv_free",
    "mpv_get_property_osd_string", "mpv_hook_add", "mpv_hook_continue", "mpv_initialize",
    "mpv_observe_property", "mpv_render_context_render", "mpv_request_log_messages",
    "mpv_set_wakeup_callback", "mpv_wait_event",
}
RUST_COMPONENTS = {
    "rustc/x86_64-unknown-linux-gnu": "572aaee68b7f1ca435e61bae5b240576b82a3aa28d1fb1b6f8c2b5017bdc0751",
    "cargo/x86_64-unknown-linux-gnu": "c05795b27e83486e6e5ef9a34cb9818789d114cf4432fbfe74d0601084030d9a",
    "rust-std/x86_64-unknown-linux-gnu": "5fdb55067ee33558d32ac7848227a7c10549b93a6c6edb796ebe83e3b8941956",
    "rust-std/x86_64-pc-windows-gnu": "834cdde42333e0a114fb659b15dd1fce1b0a791ad4e60c135019069d1e344f0e",
}
HEX40 = re.compile(r"[0-9a-f]{40}\Z")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
HEX128 = re.compile(r"[0-9a-f]{128}\Z")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_object(payload):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate key: {key}")
            result[key] = value
        return result
    return json.loads(payload.decode("utf-8"), object_pairs_hook=unique)


def read_object(path):
    return parse_object(path.read_bytes())


def check_inputs(lock):
    sources = lock.get("sources")
    if not isinstance(sources, list) or not sources:
        raise ValueError("source list missing")
    names = set()
    for item in sources:
        name = item.get("name")
        if not isinstance(name, str) or not name or name in names:
            raise ValueError("source names missing or duplicated")
        names.add(name)
        if not isinstance(item.get("url"), str) or not item["url"].startswith("https://"):
            raise ValueError(f"source URL missing: {name}")
        if item.get("kind") == "git":
            if not HEX40.fullmatch(item.get("commit", "")) or set(item["commit"]) == {"0"}:
                raise ValueError(f"source commit not pinned: {name}")
        elif item.get("kind") == "archive":
            value = item.get("sha256", "")
            value512 = item.get("sha512", "")
            valid256 = HEX64.fullmatch(value) and set(value) != {"0"}
            valid512 = HEX128.fullmatch(value512) and set(value512) != {"0"}
            if not valid256 and not valid512:
                raise ValueError(f"archive hash not pinned: {name}")
        else:
            raise ValueError(f"unsupported source kind: {name}")
    graph_path = BUILD / "feature-graph-candidate.json"
    graph_bytes = graph_path.read_bytes()
    if lock.get("candidate_graph_sha256") != hashlib.sha256(graph_bytes).hexdigest():
        raise ValueError("candidate graph differs from approved lock")
    graph = parse_object(graph_bytes)
    helper_nodes = {"x265-8+10bit", "x265-10bit-lib"}
    required = set(graph["nodes"]) - helper_nodes - {"mpv"}
    missing = required - names
    if missing:
        raise ValueError("candidate feature-root sources missing: " + ", ".join(sorted(missing)))


def check_patches(lock):
    patches = lock.get("bridge_patches")
    if not isinstance(patches, list) or not patches:
        raise ValueError("bridge patch identities missing")
    actual = {path.name: path for path in (ROOT / "patches" / "mpv").glob("[0-9][0-9][0-9][0-9]-*.patch")}
    declared = {}
    for item in patches:
        if set(item) != {"name", "sha256"}:
            raise ValueError("unexpected bridge patch fields")
        name, expected = item.get("name"), item.get("sha256", "")
        if name in declared or name not in actual or not HEX64.fullmatch(expected):
            raise ValueError("unexpected, duplicate or malformed bridge patch")
        declared[name] = expected
    if declared != PATCHES or set(actual) != set(PATCHES):
        raise ValueError("bridge patch set differs from checked-in source")
    for name, expected in declared.items():
        if digest(actual[name]) != expected:
            raise ValueError(f"bridge patch changed: {name}")


def qt_text_identity(content):
    canonical = content.replace(b"\r\n", b"\n")
    if b"\r" in canonical:
        raise ValueError("Qt source contains an unsupported lone carriage return")
    sha256 = hashlib.sha256(canonical).hexdigest()
    blob = hashlib.sha1(b"blob " + str(len(canonical)).encode() + b"\0" + canonical).hexdigest()
    return sha256, blob


def check_qt_inputs(lock):
    qt = lock.get("qt_kit", {})
    if (qt.get("version") != "6.9.3" or not HEX64.fullmatch(qt.get("configuration_sha256", "")) or
            not isinstance(qt.get("configuration_file"), str) or not qt["configuration_file"]):
        raise ValueError("Qt kit version or configuration identity missing")
    patches = qt.get("source_patches")
    if not isinstance(patches, list):
        raise ValueError("Qt bridge source identities missing")
    names = set()
    for item in patches:
        if not isinstance(item, dict) or set(item) != {"path", "sha256", "git_blob"}:
            raise ValueError("invalid Qt source identity fields")
        path, expected, blob = item["path"], item["sha256"], item["git_blob"]
        parts = path.split("/")
        if (len(parts) < 3 or parts[:2] != ["patches", "qt"] or
                any(part in ("", ".", "..") or "\\" in part or ":" in part for part in parts) or
                path in names or not HEX64.fullmatch(expected) or not HEX40.fullmatch(blob)):
            raise ValueError("invalid Qt source patch identity")
        names.add(path)
        current = ROOT
        for part in parts:
            current = current / part
            info = current.lstat()
            if current.is_symlink() or getattr(info, "st_file_attributes", 0) & 0x400:
                raise ValueError("reparse point in Qt source patch path")
        actual_sha, actual_blob = qt_text_identity(current.read_bytes())
        if actual_sha != expected:
            raise ValueError(f"Qt canonical source SHA-256 changed: {path}")
        if actual_blob != blob:
            raise ValueError(f"Qt canonical Git blob changed: {path}")
    if names != QT_BRIDGE_PATHS:
        raise ValueError("Qt bridge source path set differs from the selected Base/Quick series")


def check_configuration(lock):
    if lock.get("schema_version") != 1 or lock.get("status") != "locked":
        raise ValueError("source lock is not complete")
    if lock.get("provider_commit") != PROVIDER or lock.get("mpv_commit") != MPV:
        raise ValueError("provider or mpv revision mismatch")
    flags = lock.get("verification", {})
    for field in ("feature_graph_complete", "nested_fetches_complete", "host_tools_locked", "cargo_locked", "offline_inputs_complete"):
        if flags.get(field) is not True:
            raise ValueError(f"unresolved lock requirement: {field}")
    if lock.get("unresolved") != []:
        raise ValueError("lock has unresolved inputs")
    if lock.get("rust_manifest_sha256") != RUST_MANIFEST:
        raise ValueError("Rust channel manifest mismatch")
    if lock.get("rust_components") != RUST_COMPONENTS:
        raise ValueError("Rust component identities differ from approved selection")
    if lock.get("runtime_dependency_profile") != "windows-static-nonsystem":
        raise ValueError("static non-system runtime dependency profile required")
    variants = lock.get("variants", {})
    for key, cpu in (("x86_64", "x86-64"), ("x86_64_v3", "x86-64-v3")):
        variant = variants.get(key, {})
        if variant.get("gcc_arch") != cpu or variant.get("crt") != "ucrt":
            raise ValueError(f"incorrect target configuration: {key}")
        if variant.get("target") != "x86_64-w64-mingw32":
            raise ValueError(f"incorrect target triple: {key}")
    public = lock.get("required_public_exports")
    features = lock.get("required_features")
    private = lock.get("required_private_exports")
    if not all(isinstance(value, list) and all(isinstance(x, str) and x for x in value)
               for value in (public, features, private)):
        raise ValueError("required exports and features must be lists of nonempty strings")
    if any(len(value) != len(set(value)) for value in (public, features, private)):
        raise ValueError("duplicate required exports or features")
    if any(not re.fullmatch(r"mpv_[A-Za-z0-9_]+", name) for name in public + private):
        raise ValueError("malformed mpv export name")
    if not features or not MINIMUM_PUBLIC_EXPORTS.issubset(public):
        raise ValueError("required exports or features absent")
    if not PRIVATE_EXPORTS.issubset(private):
        raise ValueError("native frame or seek bridge exports absent")


def verify(path, approved_sha256):
    payload = path.read_bytes()
    if not HEX64.fullmatch(approved_sha256) or hashlib.sha256(payload).hexdigest() != approved_sha256:
        raise ValueError("source-lock digest does not match approved identity")
    lock = parse_object(payload)
    check_configuration(lock)
    check_inputs(lock)
    check_patches(lock)
    check_qt_inputs(lock)
    return lock


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock", required=True, type=Path)
    parser.add_argument("--approved-sha256", required=True)
    args = parser.parse_args()
    try:
        verify(args.lock, args.approved_sha256)
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(f"Recovery source-lock preflight failed: {error}", file=sys.stderr)
        return 1
    print("Recovery source-lock structure and approved digest checked; artifact verification is separate.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
