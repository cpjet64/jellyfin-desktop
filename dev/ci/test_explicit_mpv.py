import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import explicit_mpv


class ExplicitMpvTest(unittest.TestCase):
    def test_default_and_partial_inputs(self):
        self.assertIsNone(explicit_mpv.explicit_inputs({}, "Linux", Path.cwd()))
        with self.assertRaisesRegex(RuntimeError, "all three"):
            explicit_mpv.explicit_inputs({"CI_MPV_LIBRARY": "/tmp/libmpv.so"}, "Linux", Path.cwd())
        with self.assertRaisesRegex(RuntimeError, "all three"):
            explicit_mpv.explicit_inputs(dict.fromkeys(explicit_mpv.KEYS, "/x"), "Darwin", Path.cwd())

    def test_verified_inputs_and_hash_mismatch(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            root = Path(tmp)
            archive = root / "package.deb"
            archive.write_bytes(b"fixture")
            include = root / "include"
            include.mkdir()
            library = root / "libmpv.so.2"
            library.write_bytes(b"library")
            env = dict(zip(explicit_mpv.KEYS, map(str, (archive, include, library))))
            with self.assertRaisesRegex(RuntimeError, "SHA mismatch"):
                explicit_mpv.explicit_inputs(env, "Linux", root)
            with patch.object(explicit_mpv, "SHA", hashlib.sha256(b"fixture").hexdigest()), \
                 patch.object(explicit_mpv, "verify_headers") as headers, \
                 patch.object(explicit_mpv, "verify_library") as runtime:
                paths = explicit_mpv.explicit_inputs(env, "Linux", root)
            headers.assert_called_once_with(archive.resolve(), include.resolve())
            runtime.assert_called_once_with(library.resolve())
            self.assertEqual(explicit_mpv.cmake_options(paths), [
                "-DMPV_INCLUDE_DIR=" + str(include.resolve()), "-DMPV_LIBRARY_mpv=" + str(library.resolve()),
                "-DLibmpv_INCLUDE_DIRS=" + str(include.resolve() / "mpv"),
                "-DLibmpv_LIBRARIES=" + str(library.resolve())])
            library.unlink()
            with self.assertRaisesRegex(RuntimeError, "all paths exist"):
                explicit_mpv.explicit_inputs(env, "Linux", root)

    def test_package_headers_and_runtime_identity_fail_closed(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            root = Path(tmp)
            directory = root / "mpv"
            directory.mkdir()
            data = io.BytesIO()
            with tarfile.open(fileobj=data, mode="w") as tar:
                for name in ("client.h", "render.h", "render_gl.h", "stream_cb.h"):
                    payload = name.encode()
                    (directory / name).write_bytes(payload)
                    entry = tarfile.TarInfo("./usr/include/mpv/" + name)
                    entry.size = len(payload)
                    tar.addfile(entry, io.BytesIO(payload))
            with patch.object(explicit_mpv.subprocess, "run", return_value=SimpleNamespace(stdout=data.getvalue())):
                explicit_mpv.verify_headers(root / "fixture.deb", root)
                (directory / "client.h").write_bytes(b"altered")
                with self.assertRaisesRegex(RuntimeError, "differs"):
                    explicit_mpv.verify_headers(root / "fixture.deb", root)
            with patch.object(explicit_mpv.subprocess, "check_output", side_effect=["other: /lib/libmpv.so.2"]):
                with self.assertRaisesRegex(RuntimeError, "not owned"):
                    explicit_mpv.verify_library(Path("/lib/libmpv.so.2"))
            with patch.object(explicit_mpv.subprocess, "check_output", side_effect=["libmpv2:amd64: /lib/libmpv.so.2", "wrong"]):
                with self.assertRaisesRegex(RuntimeError, "version mismatch"):
                    explicit_mpv.verify_library(Path("/lib/libmpv.so.2"))

    def test_configured_library_must_match(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            root = Path(tmp)
            include, library = root / "include", root / "libmpv.so.2"
            cache = root / "CMakeCache.txt"
            cache.write_text(f"MPV_INCLUDE_DIR:PATH={include}\nMPV_LIBRARY_mpv:FILEPATH={library}\n")
            explicit_mpv.verify_cache(cache, (include, library))
            cache.write_text(f"MPV_INCLUDE_DIR:PATH={include}\nMPV_LIBRARY_mpv:FILEPATH={root / 'other'}\n")
            with self.assertRaisesRegex(RuntimeError, "MPV_LIBRARY_mpv"):
                explicit_mpv.verify_cache(cache, (include, library))


if __name__ == "__main__":
    unittest.main()
