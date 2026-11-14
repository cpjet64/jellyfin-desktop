"""Pinned, bounded source edits; not a complete offline producer."""

import hashlib

MPV_RECIPE = "cfb9b0fbaaf6013069fe3eeda7e9e9644546f09cd772dbd549f8eb7fa21c7122"
CUSTOM_STEPS = "b5dc88003f81d95e9a86c4befe1bac1472a4ec15c6d9a56a8bfd9c5fabf698a2"
MPV_COMMIT = "01e2533f3143b8940857ca419b0b367f583e05ca"
MPV_FETCH = (b"    GIT_REPOSITORY https://github.com/mpv-player/mpv.git\n"
             b"    SOURCE_DIR ${SOURCE_LOCATION}\n"
             b"    GIT_CLONE_FLAGS \"--filter=tree:0\"\n"
             b"    UPDATE_COMMAND \"\"\n")
MPV_STAGED = (b"    SOURCE_DIR ${OFFLINE_MPV_SOURCE_DIR}\n"
              b"    DOWNLOAD_COMMAND \"\"\n"
              b"    UPDATE_COMMAND \"\"\n")
START = b"function(force_rebuild_git _name)\n"
END = b"function(force_rebuild_svn _name)\n"
NO_UPDATE = b"function(force_rebuild_git _name)\nendfunction()\n\n"


def check_hash(payload, expected):
    if hashlib.sha256(payload).hexdigest() != expected:
        raise ValueError("pinned source identity mismatch")


def replace_once(payload, before, after):
    if payload.count(before) != 1:
        raise ValueError("unexpected source occurrence count")
    return payload.replace(before, after, 1)


def mpv_recipe(payload):
    """Replace only the pinned mpv ExternalProject download declaration."""
    check_hash(payload, MPV_RECIPE)
    return replace_once(payload, MPV_FETCH, MPV_STAGED)


def custom_steps(payload):
    """Remove the pinned helper that generates Git fetch and reset steps."""
    check_hash(payload, CUSTOM_STEPS)
    if payload.count(START) != 1 or payload.count(END) != 1:
        raise ValueError("unexpected helper boundaries")
    start = payload.index(START)
    end = payload.index(END)
    if start >= end or b"git fetch --filter=tree:0" not in payload[start:end]:
        raise ValueError("unexpected force-update body")
    return payload[:start] + NO_UPDATE + payload[end:]


def require_mpv_binding(source_row, patches, verify_checkout, checkout, verify_patch):
    """Delegate byte identity to pinned verifier functions before source use.

    These callbacks are an explicit future integration boundary. They must
    raise on failure and be pinned by the approved producer; callbacks are
    not supplied by untrusted input. Patch verification must bind both patch
    bytes and the reviewed post-patch tree, not merely patch filenames.
    """
    if not isinstance(source_row, dict) or source_row.get("name") != "mpv":
        raise ValueError("missing mpv source identity")
    if source_row.get("kind") != "git" or source_row.get("commit") != MPV_COMMIT:
        raise ValueError("unexpected mpv source revision")
    if not isinstance(patches, list) or not patches:
        raise ValueError("reviewed mpv bridge patches are required")
    if not callable(verify_checkout) or not callable(verify_patch):
        raise ValueError("pinned source and patch verifiers are required")
    verify_checkout(source_row, checkout)
    verify_patch(source_row, checkout, patches)
