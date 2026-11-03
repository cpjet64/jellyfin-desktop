"""Check that the user-facing QML actions reach the window state manager."""

import pathlib
import re
import unittest


QML = (pathlib.Path(__file__).resolve().parents[1] / "src/ui/webview.qml").read_text(
    encoding="utf-8"
)


class FullscreenRoutes(unittest.TestCase):
    def test_toggle_wrapper_delegates_to_window_manager(self):
        self.assertRegex(
            QML,
            r"function toggleFullscreen\(\)\s*\{\s*"
            r"components\.window\.toggleFullscreen\(\)\s*\}",
        )

    def test_browser_fullscreen_requests_use_window_manager(self):
        self.assertRegex(
            QML,
            r"function setFullScreen\(enable\)\s*\{\s*"
            r"components\.window\.setFullScreen\(enable\)\s*\}",
        )
        self.assertRegex(
            QML,
            r"onFullScreenRequested:\s*\{[^}]*"
            r"mainWindow\.setFullScreen\(request\.toggleOn\)[^}]*request\.accept\(\)",
        )

    def test_keyboard_actions_use_toggle_wrapper(self):
        actions = re.findall(r"Action\s*\{(.*?)\n  \}", QML, re.DOTALL)
        toggles = [action for action in actions if "mainWindow.toggleFullscreen()" in action]
        self.assertEqual(len(toggles), 2)
        self.assertTrue(any('"F11"' in action for action in toggles))
        self.assertTrue(any('"Alt+Return"' in action for action in toggles))


if __name__ == "__main__":
    unittest.main()
