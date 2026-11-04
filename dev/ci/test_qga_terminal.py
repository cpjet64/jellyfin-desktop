import base64
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from qga_terminal import poll_preserving_terminal


class TerminalReceiptTest(unittest.TestCase):
    def test_terminal_error_is_preserved_and_never_polled_again(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            path = Path(temporary) / "terminal.json"
            response = {"data": {"exited": True, "exitcode": 1, "signal": 9,
                                 "out-data": "b3V0", "err-data": "ZXJy",
                                 "out-truncated": False, "err-truncated": True}}
            calls = []
            def request():
                calls.append(1)
                if len(calls) > 1:
                    raise RuntimeError("process reaped")
                return response
            first = poll_preserving_terminal(42, path, request)
            self.assertEqual(first, response["data"])
            saved = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(saved["response"], response)
            self.assertEqual(saved["pid"], 42)
            self.assertTrue(saved["observed_utc"])
            with self.assertRaises(ValueError):
                base64.b64decode("invalid!", validate=True)
            self.assertEqual(poll_preserving_terminal(42, path, request), first)
            self.assertEqual(len(calls), 1)

    def test_incomplete_receipt_never_triggers_another_poll(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            path = Path(temporary) / "terminal.json"
            path.write_text("{", encoding="utf-8")
            with self.assertRaises(json.JSONDecodeError):
                poll_preserving_terminal(42, path, lambda: self.fail("unexpected poll"))

    def test_running_response_does_not_create_receipt(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            path = Path(temporary) / "terminal.json"
            self.assertEqual(poll_preserving_terminal(42, path, lambda: {"data": {"exited": False}}),
                             {"exited": False})
            self.assertFalse(path.exists())

    def test_integer_terminal_flag_is_replayed_without_polling(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            path = Path(temporary) / "terminal.json"
            expected = {"exited": 1, "out-data": "", "err-data": ""}
            self.assertEqual(poll_preserving_terminal(1, path, lambda: {"data": expected}), expected)
            self.assertEqual(poll_preserving_terminal(1, path, lambda: self.fail("unexpected poll")), expected)

    def test_uncertain_request_blocks_another_poll(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            path = Path(temporary) / "terminal.json"
            def unknown():
                raise OSError("response unavailable")
            with self.assertRaises(OSError):
                poll_preserving_terminal(42, path, unknown)
            with self.assertRaises(FileExistsError):
                poll_preserving_terminal(42, path, lambda: self.fail("unexpected poll"))

    def test_terminal_persistence_failure_blocks_another_poll(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            path = Path(temporary) / "terminal.json"
            terminal = {"data": {"exited": True, "err-data": "ZXJy"}}
            with patch("qga_terminal.json.dump", side_effect=OSError("write failed")):
                with self.assertRaises(OSError):
                    poll_preserving_terminal(42, path, lambda: terminal)
            with self.assertRaises(json.JSONDecodeError):
                poll_preserving_terminal(42, path, lambda: self.fail("unexpected poll"))


if __name__ == "__main__":
    unittest.main()
