"""Deterministic lifecycle boundary tests; no native processes are launched."""
import unittest
from build_only.lifecycle import settle


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def pause(self, duration):
        self.now += duration


class Root:
    pid = 10
    process = 10

    def __init__(self, code=0, signaled=True, close_error=False):
        self.code, self.signaled, self.close_error = code, signaled, close_error
        self.close_calls = 0

    def wait(self, timeout):
        return self.signaled

    def exit_code(self):
        return self.code

    def close(self):
        self.close_calls += 1
        if self.close_error:
            raise OSError("close")


class Handle:
    def __init__(self, pid):
        self.pid = pid

    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False


class FakeAPI:
    def __init__(self, states, members=()):
        self.states = list(states)
        self.pids = members
        self.terminated = []
        self.emergency = False
        self.member = True
        self.fail_open = False
        self.fail_terminate = False
        self.wait_result = True

    def accounting(self, job):
        return self.states.pop(0) if len(self.states) > 1 else self.states[0]

    def creation_time(self, process):
        return 100

    def members(self, job):
        return self.pids

    def open_process(self, pid):
        if self.fail_open:
            raise OSError("inaccessible")
        return Handle(pid)

    def in_job(self, process, job):
        return self.member

    def wait(self, process, timeout):
        return False if timeout == 0 else self.wait_result

    def terminate(self, process, code):
        if self.fail_terminate:
            raise OSError("terminate")
        self.terminated.append(process.pid)

    def terminate_job(self, job, code):
        self.emergency = True


class Pin:
    def __init__(self, valid=True):
        self.valid = valid

    def verify_process(self, api, process, job):
        if not self.valid:
            error = ValueError("wrong identity")
            error.identity_observation = {"operation": "process_reported_image",
                                          "process_creation_time": 100,
                                          "process_image_path": r"C:\fixture\child.exe",
                                          "input_path": None, "final_path": None, "file_id": None,
                                          "observed_size": 12, "expected_size": 13}
            raise error
        return {"creation_time": 100}


def run(api, root=None, pin=None):
    clock = Clock()
    return settle(api, object(), root or Root(), pin or Pin(), 2.0,
                  grace_seconds=0, clock=clock, pause=clock.pause)


class LifecycleTests(unittest.TestCase):
    def test_natural(self):
        api = FakeAPI([(0, 1)])
        self.assertEqual(run(api)["result"], "natural")
        self.assertFalse(api.emergency)

    def test_targeted_cleanup_does_not_touch_unrelated(self):
        api = FakeAPI([(1, 2), (1, 2), (1, 2), (0, 2)], (20,))
        result = run(api)
        self.assertEqual(result["result"], "telemetry_cleanup")
        self.assertEqual(api.terminated, [20])
        self.assertNotIn(99, api.terminated)
        self.assertFalse(api.emergency)

    def test_root_fail_timeout_and_close_failure(self):
        for root in (Root(code=1), Root(signaled=False), Root(close_error=True)):
            api = FakeAPI([(0, 1)])
            self.assertEqual(run(api, root=root)["result"], "failed")
            self.assertTrue(api.emergency)

    def test_root_observed_at_deadline_fails(self):
        clock = Clock()
        clock.now = 2.0
        api = FakeAPI([(0, 1)])
        result = settle(api, object(), Root(), Pin(), 2.0, clock=clock, pause=clock.pause)
        self.assertEqual(result["result"], "failed")
        self.assertTrue(api.emergency)

    def test_root_signaled_but_observed_at_deadline_fails(self):
        clock = Clock()

        class LateRoot(Root):
            def wait(self, timeout):
                clock.now = 2.0
                return True

        api = FakeAPI([(0, 1)])
        result = settle(api, object(), LateRoot(), Pin(), 2.0, clock=clock, pause=clock.pause)
        self.assertEqual(result["result"], "failed")
        self.assertEqual(api.terminated, [])
        self.assertTrue(api.emergency)

    def test_unresolved_root_retains_handle(self):
        root = Root(signaled=False)
        api = FakeAPI([(0, 1)])
        result = run(api, root=root)
        self.assertEqual(result["result"], "failed")
        self.assertTrue(result["emergency"])
        self.assertFalse(result["root"]["closed"])
        self.assertEqual(root.close_calls, 0)

    def test_invalid_identity_or_membership_never_targeted(self):
        for failure in ("identity", "member", "open", "truncated"):
            api = FakeAPI([(1, 2), (1, 2), (0, 2)], (20,))
            api.member = failure != "member"
            api.fail_open = failure == "open"
            if failure == "truncated":
                api.pids = ()
            result = run(api, pin=Pin(failure != "identity"))
            self.assertEqual(result["result"], "failed")
            self.assertEqual(api.terminated, [])
            self.assertTrue(api.emergency)
            if failure == "identity":
                for expected in ("pid=20", "verify_process", "ValueError", "wrong identity",
                                 "process_reported_image", "process_creation_time", "100",
                                 "process_image_path", "child.exe"):
                    self.assertIn(expected, result["error"])

    def test_new_association_before_or_after_intervention(self):
        for states in ([(1, 2), (1, 2), (1, 3), (0, 3)],
                       [(1, 2), (1, 2), (1, 2), (0, 3)]):
            api = FakeAPI(states, (20,))
            self.assertEqual(run(api)["result"], "failed")
            self.assertTrue(api.emergency)

    def test_transient_association_during_classification(self):
        api = FakeAPI([(1, 2), (1, 2), (1, 3), (0, 3)], (20,))
        result = run(api)
        self.assertEqual(result["result"], "failed")
        self.assertEqual(api.terminated, [])
        self.assertTrue(api.emergency)

    def test_terminate_failure_and_wait_timeout(self):
        for failure in ("terminate", "wait"):
            api = FakeAPI([(1, 2), (1, 2), (1, 2), (0, 2)], (20,))
            api.fail_terminate = failure == "terminate"
            api.wait_result = failure != "wait"
            self.assertEqual(run(api)["result"], "failed")
            self.assertTrue(api.emergency)

    def test_final_accounting_and_handle_close_deadline_overrun(self):
        for point in ("accounting", "close"):
            clock = Clock()

            class DelayedHandle(Handle):
                def __exit__(self, *args):
                    if point == "close":
                        clock.now = 10.0005
                    return False

            class DelayedAPI(FakeAPI):
                def open_process(self, pid):
                    return DelayedHandle(pid)

                def accounting(self, job):
                    state = super().accounting(job)
                    if point == "accounting" and state == (0, 2):
                        clock.now = 10.0005
                    return state

            api = DelayedAPI([(1, 2), (1, 2), (1, 2), (0, 2)], (20,))
            result = settle(api, object(), Root(), Pin(), 2.0, grace_seconds=0,
                            clock=clock, pause=clock.pause)
            with self.subTest(point=point):
                self.assertEqual(result["result"], "failed")
                self.assertTrue(result["emergency"])
