"""Known native desktop evidence obligations and their producer availability."""

COMMON = ("webengine", "windowed_playback", "containment")
PLATFORM = {"windows": ("windows_geometry", "windows_callback"),
            "linux": ("linux_mpris_fault", "linux_active_stop"),
            "macos": ("macos_media_callback",)}
REASONS = {
    "webengine": "Windowed WebEngine case was not run in this headless job",
    "windowed_playback": "Windowed playback cases were not run in this headless job",
    "containment": "Windowed containment cases were not run in this headless job",
    "windows_geometry": "Windowed geometry producer was not run in this headless job",
    "windows_callback": "Reviewed taskbar callback producer is not available",
    "linux_mpris_fault": "Named private-bus MPRIS case receipt was not captured",
    "linux_active_stop": "Private D-Bus active-playback Stop case has not run",
    "macos_media_callback": "Reviewed media delegate callback producer is not available",
}


def blocked_obligations(platform):
    return {name: {"status": "blocked", "reason": REASONS[name]} for name in COMMON + PLATFORM[platform]}
