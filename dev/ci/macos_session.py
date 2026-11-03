"""Verify the specifically authorized existing macOS GUI session; create none."""

import os
import platform
import subprocess

AUTHORIZED_ACCOUNT = "runner"
AUTHORIZED_HOST = "tahoe-runner-114"
AUTHORIZED_RUNNER = "12"


def account_for_uid(uid):
    import pwd
    try:
        return pwd.getpwuid(uid).pw_name
    except KeyError as error:
        raise RuntimeError("BLOCKED: macOS job UID has no account record") from error


def verify_existing_session(environment):
    environment.pop("CI_DEDICATED_GUI_SESSION", None)
    allowed_account = environment.get("CI_ALLOWED_GUI_ACCOUNT", "")
    allowed_host = environment.get("CI_ALLOWED_GUI_HOST", "").split(".")[0].lower()
    if (allowed_account, allowed_host, environment.get("CI_RUNNER_ID", "")) != (
            AUTHORIZED_ACCOUNT, AUTHORIZED_HOST, AUTHORIZED_RUNNER):
        raise RuntimeError("BLOCKED: authorized macOS runner account and host were not specified")
    uid = os.getuid()
    actual_account = account_for_uid(uid)
    actual_host = platform.node().split(".")[0].lower()
    print(f"macOS GUI identity host={actual_host} account={actual_account} uid={uid}", flush=True)
    if (actual_account, actual_host) != (allowed_account, allowed_host):
        raise RuntimeError("BLOCKED: macOS job identity does not match authorized account and host")
    try:
        result = subprocess.run(["launchctl", "print", f"gui/{uid}"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
    except (OSError, subprocess.SubprocessError) as error:
        raise RuntimeError("BLOCKED: cannot verify the existing macOS GUI session") from error
    if result.returncode:
        raise RuntimeError("BLOCKED: authorized macOS account has no existing GUI session")
    try:
        console = subprocess.check_output(["stat", "-f", "%u", "/dev/console"], text=True, timeout=15).strip()
    except (OSError, subprocess.SubprocessError) as error:
        raise RuntimeError("BLOCKED: cannot verify macOS console ownership") from error
    if console != str(uid):
        raise RuntimeError("BLOCKED: authorized macOS account does not own the console")
    environment["CI_DEDICATED_GUI_SESSION"] = "1"
    print(f"Verified existing authorized macOS GUI session and console uid={uid}", flush=True)
