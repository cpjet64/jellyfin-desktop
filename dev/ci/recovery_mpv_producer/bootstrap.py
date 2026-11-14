"""Review-bound bootstrap proposal; invoke only after independent review.

The operator must authenticate this bootstrap and the Python interpreter before
starting Python. This file checks every subsequently loaded local module's bytes
against the separately approved manifest before compiling any of those modules.
"""

import hashlib
import json
from pathlib import Path
import sys
import types

MODULES = (
    ("recovery_lock_preflight", "patches/mpv/build/recovery_lock_preflight.py"),
    ("offline_git", "patches/mpv/build/offline_git.py"),
    ("offline_inputs", "patches/mpv/build/offline_inputs.py"),
    ("adapter", "patches/mpv/build/offline_provider/adapter.py"),
    ("assembly", "dev/ci/portable_proof/assembly.py"),
    ("executor", "dev/ci/portable_proof/executor.py"),
    ("commands", "dev/ci/recovery_mpv_producer/commands.py"),
    ("plan", "dev/ci/recovery_mpv_producer/plan.py"),
    ("stage", "dev/ci/recovery_mpv_producer/stage.py"),
    ("supervisor", "dev/ci/recovery_mpv_producer/supervisor.py"),
    ("invoke", "dev/ci/recovery_mpv_producer/invoke.py"),
)


def load_reviewed(root, manifest_path, manifest_sha, expected_source):
    """Check manifest and all module bytes first; then load the fixed modules."""
    payload = Path(manifest_path).read_bytes()
    if hashlib.sha256(payload).hexdigest() != manifest_sha:
        raise ValueError("review manifest identity mismatch")
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate manifest key")
            result[key] = value
        return result
    manifest = json.loads(payload, object_pairs_hook=unique)
    if manifest.get("reviewed_source_sha") != expected_source or len(expected_source) != 40:
        raise ValueError("reviewed source SHA mismatch")
    root = Path(root).resolve(strict=True)
    verified = []
    for name, relative in MODULES:
        path = root / relative
        if any(part.is_symlink() for part in (path, *path.parents)):
            raise ValueError("linked module path")
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != manifest.get("files", {}).get(relative):
            raise ValueError("reviewed module identity mismatch: " + relative)
        verified.append((name, path, data))
    loaded = {}
    for name, path, data in verified:
        if name in sys.modules:
            raise ValueError("module namespace already occupied: " + name)
        module = types.ModuleType(name)
        module.__file__ = str(path)
        sys.modules[name] = module
        exec(compile(data, str(path), "exec"), module.__dict__)
        loaded[name] = module
    return loaded


def run(context):
    """Run only modules whose exact raw bytes match the reviewed manifest."""
    loaded = load_reviewed(context.root, context.review_manifest,
                           context.review_sha256, context.reviewed_source_sha)
    return loaded["invoke"].produce(context, loaded["recovery_lock_preflight"],
                                    loaded["offline_inputs"], loaded["adapter"],
                                    loaded["executor"])
