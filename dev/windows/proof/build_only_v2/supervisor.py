"""Disabled production boundary and exact role-pin preparation for v2.

This source proposal does not supply a compiler-launch entry point. A later
qualified integration must retain all v2 and reused v1 module bytes before
import, and prove endpoint isolation independently before enabling admission.
"""
from contextlib import contextmanager, ExitStack


@contextmanager
def retained_role_pins(api, profile, file_pin):
    """Retain both reviewed images until the caller exits this boundary."""
    from .profile import validate_profile
    validate_profile(profile)
    pins = {}
    with ExitStack() as stack:
        for role in profile.residual_roles:
            pin = stack.enter_context(file_pin(api, role.path, role.sha256, role.size))
            if ':'.join(str(part) for part in pin.file_id) != role.file_id:
                raise ValueError('role file identity mismatch')
            pins[role.role] = pin
        if len({pin.file_id for pin in pins.values()}) != 2:
            raise ValueError('role images alias')
        for pin in pins.values():
            pin.assert_path_unchanged()
        yield pins
        for pin in pins.values():
            pin.assert_path_unchanged()


def production(*_args, **_kwargs):
    raise RuntimeError('production disabled pending exact compiler endpoint qualification')
