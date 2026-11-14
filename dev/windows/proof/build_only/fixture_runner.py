"""Fixture-only supervisor entry point for explicitly allocated qualification."""
from dataclasses import fields
import json
import sys
from .policy import CommandIdentity, Policy
from .supervisor import fixture


def _unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate manifest field")
        result[key] = value
    return result


def _construct(cls, value):
    if not isinstance(value, dict) or set(value) != {field.name for field in fields(cls)}:
        raise ValueError("invalid manifest fields")
    return cls(**value)


def main(manifest, receipt):
    with open(manifest, "rb") as stream:
        data = json.load(stream, object_pairs_hook=_unique)
    if not isinstance(data, dict) or data.get("kind") != "fixture" or data.get("command_form") != "fixture":
        raise ValueError("fixture policy required")
    command = data.get("command")
    if not isinstance(command, dict):
        raise ValueError("command missing")
    for field in ("args", "outputs", "env_names"):
        if not isinstance(command.get(field), list) or not all(isinstance(item, str) for item in command[field]):
            raise ValueError("invalid command list")
        command[field] = tuple(command[field])
    data["command"] = _construct(CommandIdentity, command)
    policy = _construct(Policy, data)
    fixture(policy, receipt)
    return 0


if __name__ == "__main__":
    if sys.platform != "win32" or len(sys.argv) != 3:
        raise SystemExit(125)
    raise SystemExit(main(sys.argv[1], sys.argv[2]))
