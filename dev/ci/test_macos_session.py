"""Test authorization decisions without opening or altering a GUI session."""

from contextlib import ExitStack, redirect_stdout
import io
import sys
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
import macos_session


class Result:
    def __init__(self, returncode):
        self.returncode = returncode


class TestMacSession(unittest.TestCase):
    def verify(self, *, host="tahoe-runner-114", account="runner", gui=0, console="501", runner="12", allowed="tahoe-runner-114"):
        environment = {"CI_ALLOWED_GUI_ACCOUNT": "runner", "CI_ALLOWED_GUI_HOST": allowed,
                       "CI_RUNNER_ID": runner, "CI_DEDICATED_GUI_SESSION": "1"}
        with ExitStack() as stack:
            stack.enter_context(redirect_stdout(io.StringIO()))
            stack.enter_context(patch.object(macos_session.os, "getuid", return_value=501, create=True))
            stack.enter_context(patch.object(macos_session, "account_for_uid", return_value=account))
            stack.enter_context(patch.object(macos_session.platform, "node", return_value=host))
            stack.enter_context(patch.object(macos_session.subprocess, "run", return_value=Result(gui)))
            stack.enter_context(patch.object(macos_session.subprocess, "check_output", return_value=console))
            try:
                macos_session.verify_existing_session(environment)
            except RuntimeError:
                self.assertNotIn("CI_DEDICATED_GUI_SESSION", environment)
                raise
        return environment

    def test_authorized_existing_session(self):
        self.assertEqual(self.verify(host="tahoe-runner-114.local")["CI_DEDICATED_GUI_SESSION"], "1")

    def test_wrong_account_host_runner_or_console_fails(self):
        for arguments in ({"account": "other"}, {"host": "other"}, {"runner": "99"}, {"console": "0"}, {"allowed": "other"}):
            with self.subTest(arguments=arguments), self.assertRaises(RuntimeError):
                self.verify(**arguments)

    def test_missing_gui_fails(self):
        with self.assertRaisesRegex(RuntimeError, "no existing GUI session"):
            self.verify(gui=1)


if __name__ == "__main__":
    unittest.main()
