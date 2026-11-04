import hashlib
import io
import os
from pathlib import Path
import tarfile
import tempfile
import subprocess
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import explicit_mpv
import linux_mpv_payload as payload
import linux_sdk_probe as probe


def archive(entries):
    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode="w") as output:
        for name, content in entries:
            item = tarfile.TarInfo("./usr/include/mpv/" + name)
            item.size = len(content)
            output.addfile(item, io.BytesIO(content))
    return data.getvalue()


class LinuxInputsTest(unittest.TestCase):
    def test_bad_download_does_not_extract_or_verify(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            with patch.object(payload.urllib.request, "urlopen", return_value=io.BytesIO(b"wrong")), \
                 patch.object(payload, "extract_headers") as extract, \
                 patch.object(payload, "explicit_inputs") as verify:
                with self.assertRaisesRegex(RuntimeError, "size or SHA mismatch"):
                    payload.acquire_mpv(Path(tmp))
                extract.assert_not_called()
                verify.assert_not_called()

    def test_only_selected_headers_are_extracted_and_duplicates_rejected(self):
        entries = [(name, name.encode()) for name in payload.HEADERS] + [("other.h", b"other")]
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            root = Path(tmp)
            with patch.object(payload.subprocess, "run", return_value=SimpleNamespace(stdout=archive(entries))):
                payload.extract_headers(root / "fixture.deb", root)
            self.assertEqual(set(p.name for p in root.iterdir()), set(payload.HEADERS))
            with patch.object(payload.subprocess, "run", return_value=SimpleNamespace(stdout=archive(entries + entries[:1]))):
                with self.assertRaisesRegex(RuntimeError, "invalid mpv header"):
                    payload.extract_headers(root / "fixture.deb", root)

    def test_pinned_runtime_digest_only_applies_to_opted_in_route(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            root = Path(tmp)
            archive_file, library, include = root / "x.deb", root / "libmpv.so", root / "include"
            archive_file.write_bytes(b"package")
            library.write_bytes(b"runtime")
            include.mkdir()
            env = dict(zip(explicit_mpv.KEYS, map(str, (archive_file, include, library))))
            with patch.object(explicit_mpv, "SHA", hashlib.sha256(b"package").hexdigest()), \
                 patch.object(explicit_mpv, "verify_headers"), patch.object(explicit_mpv, "verify_library"):
                explicit_mpv.explicit_inputs(env, "Linux", root)
                env["CI_MPV_PINNED_RUNTIME"] = "1"
                with self.assertRaisesRegex(RuntimeError, "runtime SHA mismatch"):
                    explicit_mpv.explicit_inputs(env, "Linux", root)

    def test_metadata_rejects_system_prefix_and_wrong_version(self):
        qt = Path("/selected/qt").resolve()
        with patch.object(probe.Path, "is_file", return_value=True), patch.object(probe, "beneath", return_value=True), \
             patch.object(probe, "run", side_effect=["6.9.2"]):
            with self.assertRaisesRegex(RuntimeError, "qmake6"):
                probe.check_qt_metadata(qt, {})
        with patch.object(probe.Path, "is_file", return_value=True), patch.object(probe, "beneath", return_value=True), \
             patch.object(probe, "run", side_effect=["6.9.3", str(qt), "6.9.3", str(qt), "6.9.3", "/usr"]):
            with self.assertRaisesRegex(RuntimeError, "prefix differs"):
                probe.check_qt_metadata(qt, {})

    def test_missing_private_header_is_rejected(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            with self.assertRaisesRegex(RuntimeError, "Qt private header missing"):
                probe.check_private_headers(Path(tmp))

    def test_runtime_probe_rejects_alternate_mpv_and_qt_prefix(self):
        with patch.object(probe, "run", side_effect=RuntimeError("Loaded libmpv identity does not match")):
            with self.assertRaisesRegex(RuntimeError, "libmpv identity"):
                probe.check_executable(Path("build"), Path("libmpv.so"), Path("qt"), {})
        with patch.object(probe, "run", return_value="6.9.3\nother-prefix"):
            with self.assertRaisesRegex(RuntimeError, "QLibraryInfo"):
                probe.check_executable(Path("build"), Path("libmpv.so"), Path("qt"), {})

    def test_private_target_mixed_prefix_rejected(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            root = Path(tmp)
            qt, build, other = root / "qt", root / "build", root / "other"
            for path in (qt, build, other):
                path.mkdir()
            for module in ("Gui", "Quick", "WebEngineCore"):
                (build / ("Qt6_" + module + "Private-includes.txt")).write_text(str(qt))
            probe.check_private_targets(build, qt.resolve())
            (build / "Qt6_QuickPrivate-includes.txt").write_text(str(other))
            with self.assertRaisesRegex(RuntimeError, "nonlocal includes"):
                probe.check_private_targets(build, qt.resolve())

    def test_graphics_route_and_tool_path_with_real_cmake(self):
        module = (Path(__file__).parent / "linux_sdk_probe/probe_checks.cmake").resolve().as_posix()
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            root = Path(tmp)
            qt = root / "qt"
            qt.mkdir()
            local, other = qt / "moc", root / "other"
            local.touch()
            other.touch()
            script = root / "probe.cmake"
            script.write_text(f'include("{module}")\nset(QTROOT "{qt.as_posix()}")\n'
                              'select_graphics("${GL}" "gl-lib" "${GLES}" "gles-lib")\n'
                              'if(NOT PROBE_GRAPHICS_DEFINE STREQUAL EXPECT)\nmessage(FATAL_ERROR "wrong route")\nendif()\n'
                              'require_qt_path("${TOOL}" "tool")\n')
            def invoke(gl, gles, expected, tool):
                return subprocess.run(["cmake", "-DGL=" + gl, "-DGLES=" + gles, "-DEXPECT=" + expected,
                                       "-DTOOL=" + tool.as_posix(), "-P", str(script)], capture_output=True, timeout=30,
                                      creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            self.assertEqual(invoke("ON", "OFF", "PROBE_OPENGL", local).returncode, 0)
            self.assertEqual(invoke("OFF", "ON", "PROBE_GLES2", local).returncode, 0)
            self.assertNotEqual(invoke("OFF", "OFF", "unused", local).returncode, 0)
            self.assertNotEqual(invoke("ON", "OFF", "PROBE_OPENGL", other).returncode, 0)


if __name__ == "__main__":
    unittest.main()
