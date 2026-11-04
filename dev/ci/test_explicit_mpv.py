import hashlib
import io
from pathlib import Path
import shutil
import subprocess
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
                "-DJELLYFIN_EXPLICIT_MPV=ON", "-DMPV_INCLUDE_DIR=" + str(include.resolve()), "-DMPV_LIBRARY_mpv=" + str(library.resolve()),
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
                (directory / "client.h").write_bytes(b"client.h")
                (directory / "render.h").unlink()
                with self.assertRaisesRegex(RuntimeError, "missing extracted"):
                    explicit_mpv.verify_headers(root / "fixture.deb", root)
            with patch.object(explicit_mpv.subprocess, "check_output", side_effect=["other: /lib/libmpv.so.2"]):
                with self.assertRaisesRegex(RuntimeError, "not owned"):
                    explicit_mpv.verify_library(Path("/lib/libmpv.so.2"))
            with patch.object(explicit_mpv.subprocess, "check_output", side_effect=["libmpv2:amd64: /lib/libmpv.so.2", "installed\namd64\nwrong"]):
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

    def test_runtime_package_integrity_architecture_and_status(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            root = Path(tmp)
            library = root / "libmpv.so.2"
            image = b"\x7fELF\x02\x01" + bytes(12) + b"\x3e\x00" + b"fixture"
            library.write_bytes(image)
            digest = hashlib.md5(image, usedforsecurity=False).hexdigest()
            (root / "libmpv2.md5sums").write_text(digest + "  libmpv.so.2\n")
            explicit_mpv.verify_installed_bytes(library, "libmpv2", root, root)
            library.write_bytes(b"altered")
            with self.assertRaisesRegex(RuntimeError, "bytes differ"):
                explicit_mpv.verify_installed_bytes(library, "libmpv2", root, root)
            wrong_arch = image[:18] + b"\xb7\x00" + image[20:]
            library.write_bytes(wrong_arch)
            (root / "libmpv2.md5sums").write_text(hashlib.md5(wrong_arch, usedforsecurity=False).hexdigest() + "  libmpv.so.2\n")
            with self.assertRaisesRegex(RuntimeError, "not x86_64 ELF"):
                explicit_mpv.verify_installed_bytes(library, "libmpv2", root, root)
            for metadata in ("installed\narm64\n0.37.0-1ubuntu4", "not-installed\namd64\n0.37.0-1ubuntu4"):
                with patch.object(explicit_mpv.subprocess, "check_output", side_effect=["libmpv2:amd64: /lib/a", metadata]):
                    with self.assertRaisesRegex(RuntimeError, "not installed for amd64"):
                        explicit_mpv.verify_library(Path("/lib/a"))
            with patch.object(explicit_mpv.subprocess, "check_output", side_effect=["libmpv2:amd64: /lib/a", "installed\namd64\n0.37.0-1ubuntu4"]), \
                 patch.object(explicit_mpv, "verify_installed_bytes") as verify:
                explicit_mpv.verify_library(Path("/lib/a"))
            verify.assert_called_once_with(Path("/lib/a"), "libmpv2:amd64")

    def test_actual_findmpv_ignores_stale_pkg_selection_only_in_explicit_mode(self):
        self.assertIsNotNone(shutil.which("cmake"), "cmake is required for this check")
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            root = Path(tmp)
            module = (Path(__file__).resolve().parents[2] / "CMakeModules/FindMPV.cmake").as_posix()
            include, expected, other = (root / name for name in ("include", "mpv.lib", "other.lib"))
            include.mkdir()
            expected.touch()
            other.touch()
            script = root / "check.cmake"
            script.write_text('''set(WIN32 FALSE)
set(PKG_CONFIG_FOUND TRUE)
macro(pkg_check_modules)
  set(PC_MPV_LIBRARIES other)
endmacro()
set(PC_MPV_LIBRARIES other)
set(MPV_INCLUDE_DIR "@INCLUDE@")
set(MPV_LIBRARY_mpv "@EXPECTED@")
set(MPV_LIBRARY_other "@OTHER@")
include("@MODULE@")
if(NOT MPV_LIBRARY STREQUAL WANT)
  message(FATAL_ERROR "unexpected effective library: ${MPV_LIBRARY}")
endif()
'''.replace("@INCLUDE@", include.as_posix()).replace("@EXPECTED@", expected.as_posix())
               .replace("@OTHER@", other.as_posix()).replace("@MODULE@", module))
            for explicit, want in (("ON", expected), ("OFF", other)):
                subprocess.run(["cmake", "-DJELLYFIN_EXPLICIT_MPV=" + explicit, "-DWANT=" + want.as_posix(),
                                "-P", str(script)], check=True, capture_output=True, text=True, timeout=30)

    def test_default_route_rejects_cached_explicit_configuration(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            cache = Path(tmp) / "CMakeCache.txt"
            self.assertEqual(explicit_mpv.default_options(cache), ["-DJELLYFIN_EXPLICIT_MPV=OFF"])
            cache.write_text("JELLYFIN_EXPLICIT_MPV:UNINITIALIZED=ON\n")
            with self.assertRaisesRegex(RuntimeError, "fresh build directory"):
                explicit_mpv.default_options(cache)


if __name__ == "__main__":
    unittest.main()
