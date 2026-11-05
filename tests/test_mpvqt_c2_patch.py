"""Verify the diagnostic patch against the pinned, composed MpvQt source."""

import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get("MPVQT_TEST_SOURCE", ROOT / "external/mpvqt"))


def pins(path, section):
    text = path.read_text(encoding="utf-8")
    block = text.split(f"set({section}", 1)[1].split(")", 1)[0]
    return dict(re.findall(r'"([^|]+)\|([0-9a-f]{64})"', block))


def digest(data):
    return hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()


class DiagnosticPatchTest(unittest.TestCase):
    def test_c1_and_c2_apply_to_pinned_source_and_match_output(self):
        c1 = ROOT / "CMakeModules/PatchedMpvQt.cmake"
        c2 = ROOT / "CMakeModules/PatchedMpvQtC2.cmake"
        before = pins(c1, "_mpvqt_files")
        after_c1 = pins(c2, "_c2_input_hashes")
        after_c2 = pins(c2, "_c2_output_hashes")
        with tempfile.TemporaryDirectory(prefix="mpvqt-c2-", dir=ROOT) as temp:
            copy = Path(temp)
            (copy / "src").mkdir()
            for name, expected in before.items():
                data = (SOURCE / "src" / name).read_bytes().replace(b"\r\n", b"\n")
                self.assertEqual(digest(data), expected, name)
                (copy / "src" / name).write_bytes(data)
            env = os.environ.copy()
            env["GIT_CEILING_DIRECTORIES"] = str(copy.parent)
            for patch_name, expected_hashes in (("0001-event-ownership.patch", after_c1),
                                                ("0002-render-retirement.patch", after_c2)):
                patch = (ROOT / "patches/mpvqt" / patch_name).read_bytes().replace(b"\r\n", b"\n")
                result = subprocess.run(
                    ["git", "apply", "--no-index", "--unidiff-zero", "--whitespace=nowarn", "-"],
                    cwd=copy, input=patch, capture_output=True, env=env, timeout=60,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
                    check=False)
                self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
                actual_hashes = {name: digest((copy / "src" / name).read_bytes())
                                 for name in expected_hashes}
                self.assertEqual(actual_hashes, expected_hashes, patch_name)
                if patch_name == "0002-render-retirement.patch":
                    item = (copy / "src/mpvabstractitem.cpp").read_text(encoding="utf-8")
                    for before, call, after in (
                        ("unregisterEnteredNs", "mpv_render_context_set_update_callback(owner->context, nullptr, nullptr)", "unregisterReturnedNs"),
                        ("freeEnteredNs", "mpv_render_context_free(owner->context)", "freeReturnedNs"),
                    ):
                        self.assertLess(item.index(before), item.index(call))
                        self.assertLess(item.index(call), item.index(after))
                    self.assertLess(item.index("ackEmittedNs"), item.index("emit renderRetired(generation)"))
                    renderer = (copy / "src/mpvrenderer.cpp").read_text(encoding="utf-8")
                    render = renderer.split("void MpvRenderer::render()", 1)[1].split(
                        "QOpenGLFramebufferObject *MpvRenderer::createFramebufferObject", 1)[0]
                    ordered = ("const int injected =", "const int result =", "if (!injected &&",
                               "if (result < 0 && owner->state.failOnce(",
                               "const bool original =", "owner->service.activate(",
                               "DiagnosticFailureInjection::failurePublishedNs")
                    positions = [render.index(marker) for marker in ordered]
                    self.assertEqual(positions, sorted(positions))
                    self.assertEqual(render.count("owner->service.activate("), 1)
                    self.assertNotIn("}", render[positions[3]:positions[6]])
                    self.assertLess(renderer.index("failurePublishedNs"), renderer.index("QMetaObject::invokeMethod(item", renderer.index("failurePublishedNs")))
                    create = renderer.split("int MpvAbstractItem::createDiagnosticRenderContext()", 1)[1]
                    branch = create.index("if (!injected)")
                    entry = create.index("createCallEnteredNs", branch)
                    count = create.index("++DiagnosticFailureInjection::createCallCount", entry)
                    call = create.index("result = mpv_render_context_create(", count)
                    result = create.index("createCallResult = result", call)
                    returned = create.index("createCallReturnedNs", result)
                    self.assertTrue(branch < entry < count < call < result < returned)


if __name__ == "__main__":
    unittest.main()
