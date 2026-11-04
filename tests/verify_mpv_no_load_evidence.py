"""Verify the intentionally failing, no-load child left complete contained evidence."""
import argparse
import json
from pathlib import Path
import re


def verify(directory):
    qtest = (directory / "qtest.txt").read_text(encoding="utf-8")
    lifecycle = json.loads((directory / "mpv-lifecycle.json").read_text(encoding="utf-8"))
    console = (directory / "console.log").read_text(encoding="utf-8")
    assert re.search(r"(?m)^FAIL!\s*:\s*TestMpvThreadIndependence::healthyPlayback\(\)", qtest)
    assert "controlled failure after binding and before media submission" in qtest
    assert re.search(r"(?m)^Totals:\s+2 passed, 1 failed, 0 skipped", qtest)
    assert "Finished testing of TestMpvThreadIndependence" in qtest
    assert re.search(r"(?m)^app wait 0 exit [1-9]\d*\s*$", console)
    assert "activeBeforeKnown true activeBefore 0 activeBeforeError 0" in console
    assert "terminationRequested false terminationSucceeded false terminationError 0" in console
    assert "activeAfterKnown true activeAfter 0 activeAfterError 0 overall fail" in console
    assert lifecycle["bound"] and lifecycle["renderer_ready"] and lifecycle["resources_settled"]
    assert not lifecycle["submission_attempted"] and not lifecycle["stop_attempted"]
    assert lifecycle["submission_at"] == 0 and lifecycle["stop_entered"] == 0
    assert lifecycle["stop_returned"] == 0 and lifecycle["stop_status"] == -1
    assert not lifecycle["operation_completed"] and lifecycle["canceled_count"] == 0
    assert lifecycle["ack_count"] == 1 and lifecycle["frees"] == 1
    assert lifecycle["binding_released"] and lifecycle["worker_joined"]
    assert lifecycle["cleanup_count"] == 1 and lifecycle["context_destroyed"] and lifecycle["surface_destroyed"]
    assert (0 < lifecycle["terminal_confirmed"] < lifecycle["retirement_requested"] <
            lifecycle["retirement_acknowledged"] < lifecycle["shutdown_returned"])
    assert 0 < lifecycle["ack_order"] < lifecycle["item_order"] < lifecycle["core_order"]
    assert 0 < lifecycle["cleanup_ack_order"] < lifecycle["worker_join_order"] < lifecycle["surface_destroy_order"]
    terminal = lifecycle["properties"]
    assert terminal["terminal_force_window"]["status"] == 0
    assert terminal["terminal_force_window"]["value"] == "no"
    assert terminal["terminal_idle"]["status"] == 0 and terminal["terminal_idle"]["value"] == 1
    assert terminal["terminal_current_vo"]["status"] == -10  # MPV_ERROR_PROPERTY_UNAVAILABLE
    assert not lifecycle["diagnostics"] and len(lifecycle["album_art_log"]) == 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    verify(parser.parse_args().directory)
    print("controlled no-load failure evidence verified")
