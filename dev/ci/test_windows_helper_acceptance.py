import unittest

from acceptance import clean_windows_helper


class WindowsHelperAcceptance(unittest.TestCase):
    def test_failure_and_unknown_cannot_pass(self):
        clean = ("app wait 0 exit 0\n"
                 "waitResult 0 waitError 0 exitKnown true exitError 0\n"
                 "activeBeforeKnown true activeBefore 0 activeBeforeError 0\n"
                 "terminationRequested false terminationSucceeded false terminationError 0\n"
                 "activeAfterKnown true activeAfter 0 activeAfterError 0 overall pass\n"
                 "forcedCleanup false; job active processes 0\n")
        self.assertTrue(clean_windows_helper(clean))
        self.assertTrue(clean_windows_helper(clean.replace("\n", "\r\n")))
        for changed in (clean.replace("exit 0\n", "exit 42\n"),
                        clean.replace("exitKnown true", "exitKnown false"),
                        clean.replace("activeBeforeKnown true", "activeBeforeKnown false"),
                        clean.replace("activeAfterKnown true", "activeAfterKnown false"),
                        clean.replace("overall pass", "overall fail"),
                        clean + "app wait 0 exit 42\n"):
            with self.subTest(log=changed):
                self.assertFalse(clean_windows_helper(changed))


if __name__ == "__main__":
    unittest.main()
