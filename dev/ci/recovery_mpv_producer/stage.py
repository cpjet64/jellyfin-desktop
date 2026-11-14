"""Raw SDK staging and inventory; final Windows kit validation is separate."""

import hashlib
import json
from pathlib import Path
import shutil


def digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            sha.update(chunk)
    return sha.hexdigest()


def stage_sdk(workspace, identities):
    """Copy only the fixed expected provider outputs; reject missing or links."""
    workspace = Path(workspace)
    destination = workspace / "raw-sdk"
    destination.mkdir(exist_ok=False)
    records = []
    for variant in ("baseline", "v3"):
        binary = workspace / "build" / variant / "mpv-prefix/src/mpv-build"
        source = workspace / "sources/mpv/include/mpv"
        files = [(binary / name, name) for name in ("libmpv-2.dll", "libmpv.dll.a")]
        files += [(source / name, "include/mpv/" + name) for name in
                  ("client.h", "stream_cb.h", "render.h", "render_gl.h")]
        for original, name in files:
            if original.is_symlink() or not original.is_file():
                raise ValueError("missing or linked SDK input: " + str(original))
            target = destination / variant / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with original.open("rb") as src, target.open("xb") as dst:
                shutil.copyfileobj(src, dst)
            records.append({"path": target.relative_to(destination).as_posix(),
                            "size": target.stat().st_size, "sha256": digest(target)})
    receipt = {"kind": "raw-gnu-sdk", "final_windows_kit": False,
               "identities": identities, "files": records}
    (workspace / "raw-sdk-provenance.json").write_text(
        json.dumps(receipt, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return receipt
