import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from qga_terminal import poll_preserving_terminal


class TerminalReceiptTest(unittest.TestCase):
    def test_nonzero_response_is_exact_and_never_polled_again(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            path = Path(temporary) / "terminal.json"
            raw = b'{ "data": {"exited":true,"exitcode":1,"signal":9,"out-data":"b3V0", "err-data":"ZXJy", "err-truncated":true} }'
            calls = []
            def request():
                calls.append(1)
                if len(calls) > 1:
                    raise OSError("reaped")
                return io.BytesIO(raw)
            status = poll_preserving_terminal(42, path, request)
            self.assertEqual(path.read_bytes(), raw)
            metadata = json.loads(path.with_name(path.name + ".complete").read_text())
            self.assertEqual(metadata["pid"], 42)
            self.assertTrue(metadata["observed_utc"])
            self.assertEqual(poll_preserving_terminal(42, path, request), status)
            self.assertEqual(len(calls), 1)

    def test_invalid_json_is_preserved_without_repoll(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            path = Path(temporary) / "terminal.json"
            for _ in range(2):
                with self.assertRaises(json.JSONDecodeError):
                    poll_preserving_terminal(42, path, lambda: io.BytesIO(b"{"))
            self.assertEqual(path.read_bytes(), b"{")

    def test_pending_is_removed_for_next_poll(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            path = Path(temporary) / "terminal.json"
            status = poll_preserving_terminal(42, path, lambda: io.BytesIO(b'{"data":{"exited":false}}'))
            self.assertFalse(status["exited"])
            self.assertFalse(path.exists())
            self.assertFalse(path.with_name(path.name + ".polling").exists())

    def test_failed_or_oversized_response_is_preserved_and_blocks_poll(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            path = Path(temporary) / "terminal.json"
            with patch("qga_terminal.MAX_BODY", 4):
                with self.assertRaises(ValueError):
                    poll_preserving_terminal(42, path, lambda: io.BytesIO(b"12345"))
            self.assertEqual(path.read_bytes(), b"1234")
            with self.assertRaises(RuntimeError):
                poll_preserving_terminal(42, path, lambda: self.fail("unexpected poll"))


if __name__ == "__main__":
    unittest.main()
