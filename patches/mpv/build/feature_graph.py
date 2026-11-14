"""Derive a labeled static candidate graph without configuring the provider."""

import argparse
import json
from pathlib import Path

CONDITIONAL = {
    "ffmpeg": ["uavs3d", "davs2", "x265-8+10bit"],
    "x265-8+10bit": ["x265", "x265-10bit-lib"],
    "x265-10bit-lib": ["x265"],
}


def closure(packages):
    pending, seen, nodes = ["mpv"], set(), {}
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        recipe = packages.get(name)
        dependencies = set(CONDITIONAL.get(name, []))
        if recipe:
            dependencies.update(recipe["static_named_dependencies"])
        elif name not in CONDITIONAL:
            raise ValueError(f"unrecognized graph node: {name}")
        nodes[name] = {
            "recipe_path": recipe["path"] if recipe else "packages/x265.cmake",
            "recipe_sha256": recipe["source_sha256"] if recipe else packages["x265"]["source_sha256"],
            "candidate_dependencies": sorted(dependencies),
            "unresolved_dependency_edges": recipe["unresolved_dependency_edges"] if recipe else [],
        }
        pending.extend(dependencies)
    return {name: nodes[name] for name in sorted(nodes)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("catalog", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
    if catalog["provider_commit"] != "1f8d2e76255abc48f0456d9be99b4cab9c6566e6":
        raise ValueError("unexpected provider commit")
    graph = closure(catalog["packages"])
    result = {
        "status": "UNRESOLVED static graph; not a configured closure or lock",
        "root": "mpv",
        "profile": "provider mpv libmpv feature recipe; x86_64 baseline and proposed v3",
        "conditional_source": "x86-conditional-resolution.json",
        "nodes": graph,
        "limitations": [
            "Named dependencies are lexical and include unevaluated conditions.",
            "Only three FFmpeg x86 conditional edges and their selected x265 helper edges are expanded.",
            "Implicit downloads, submodules, archive internals, toolchain and host bootstrap remain separate.",
            "Feature compatibility, source object selection and an offline build have not been established.",
        ],
    }
    with args.output.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")


if __name__ == "__main__":
    main()
