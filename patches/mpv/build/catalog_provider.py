"""Catalog an already retrieved provider tree; never fetch or execute its recipes."""

import argparse
import hashlib
import json
import re
from pathlib import Path

PROVIDER = "1f8d2e76255abc48f0456d9be99b4cab9c6566e6"
FIELD = re.compile(r"^\s*(GIT_[A-Z_]+|URL(?:_HASH)?|DOWNLOAD_COMMAND|UPDATE_COMMAND|PATCH_COMMAND)\b(.*)$")
VARIABLE = re.compile(r"\$\{[^}]+\}|\$<[^>]+>")
UPPER = re.compile(r"^\s*[A-Z][A-Z0-9_]*(?:\s|$)")


def verified_payload(root, item):
    path = root / item["path"]
    payload = path.read_bytes()
    blob = hashlib.sha1(b"blob " + str(len(payload)).encode() + b"\0" + payload).hexdigest()
    if blob != item["sha"] or len(payload) != item["size"]:
        raise ValueError(f"provider blob mismatch: {item['path']}")
    return payload, blob


def source_identity(root, item):
    payload, blob = verified_payload(root, item)
    return {
        "path": item["path"], "git_blob_sha1": blob,
        "pinned_source_url": f"https://github.com/shinchiro/mpv-winbuild-cmake/blob/{PROVIDER}/{item['path']}",
        "source_sha256": hashlib.sha256(payload).hexdigest(),
        "source_lines": payload.decode("utf-8").splitlines(),
    }


def binary_identity(root, item):
    payload, blob = verified_payload(root, item)
    return {"path": item["path"], "git_blob_sha1": blob, "size": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
            "inspection": "archive bytes hashed; not extracted or executed"}


def declared_names(lines):
    text = "\n".join(lines)
    match = re.search(r"list\(APPEND ep\s*(.*?)\)", text, re.S)
    if not match:
        raise ValueError("initial provider package list missing")
    names = match.group(1).split()
    if len(names) != 106 or len(set(names)) != len(names):
        raise ValueError("provider package list differs from audited input")
    return names


def dependency_sections(lines):
    sections = []
    for index, line in enumerate(lines):
        if not re.match(r"^\s*DEPENDS(?:\s|$)", line):
            continue
        values = [re.sub(r"^\s*DEPENDS\s*", "", line)]
        for following in lines[index + 1:]:
            if UPPER.match(following) or re.match(r"^\s*\)", following):
                break
            values.append(following)
        sections.append({"line": index + 1, "text": "\n".join(values)})
    return sections


def summarize(record, names):
    lines = record["source_lines"]
    directives = []
    for number, line in enumerate(lines, 1):
        match = FIELD.match(line)
        if match:
            directives.append({"line": number, "field": match.group(1), "value": match.group(2).strip()})
    sections = dependency_sections(lines)
    static = set()
    unresolved_edges = []
    for section in sections:
        expressions = sorted(set(VARIABLE.findall(section["text"])))
        for expression in expressions:
            unresolved_edges.append({"line": section["line"], "expression": expression})
        for token in re.findall(r"[A-Za-z0-9_.+-]+", VARIABLE.sub(" ", section["text"])):
            if token in names:
                static.add(token)
            elif token not in ("",):
                unresolved_edges.append({"line": section["line"], "unmatched_token": token})
    variables = sorted(set(VARIABLE.findall("\n".join(lines))))
    tags = [entry["value"] for entry in directives if entry["field"] == "GIT_TAG"]
    repositories = [entry for entry in directives if entry["field"] == "GIT_REPOSITORY"]
    if repositories and not tags:
        tag_status = "no GIT_TAG declared; initial source is not pinned by this recipe"
    elif any(not re.fullmatch(r"[0-9a-fA-F]{40}", tag.strip('"')) for tag in tags):
        tag_status = "tag or expression requires independent immutable resolution"
    elif repositories:
        tag_status = "full commit-shaped tag declared; verify origin and submodules"
    else:
        tag_status = "no direct Git repository declared; inspect all source lines"
    record.update({"directives": directives, "dependency_sections": sections,
                   "static_named_dependencies": sorted(static),
                   "unresolved_dependency_edges": unresolved_edges,
                   "dynamic_expressions": variables, "git_tag_status": tag_status})
    return record


def static_closure(packages):
    seen, queue = set(), ["mpv"]
    while queue:
        name = queue.pop()
        if name in seen:
            continue
        seen.add(name)
        queue.extend(packages[name]["static_named_dependencies"])
    return sorted(seen)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    tree = json.loads((args.source / "tree.json").read_text(encoding="utf-8"))
    if tree.get("sha") != PROVIDER or tree.get("truncated"):
        raise ValueError("unexpected or truncated provider tree")
    items = {item["path"]: item for item in tree["tree"] if item["type"] == "blob"}
    index = source_identity(args.source, items["packages/CMakeLists.txt"])
    names = declared_names(index["source_lines"])
    packages = {}
    for name in names:
        item = items[f"packages/{name}.cmake"]
        packages[name] = summarize(source_identity(args.source, item), names)
    support = []
    for path, item in sorted(items.items()):
        include = path in ("CMakeLists.txt", ".gitmodules", ".gitignore", "README.md")
        include |= path.startswith(("cmake/", "toolchain/", "scripts/", ".github/workflows/"))
        include |= path.startswith("packages/") and path.endswith((".patch", ".in"))
        include |= path.startswith("packages/") and path.endswith(".cmake") and Path(path).stem not in names
        include |= path in ("exec.in", "meson_cross.txt.in", "toolchain.cmake.in")
        if include and not path.endswith(".zip") and (args.source / path).is_file():
            support.append(source_identity(args.source, item))
    binary_inputs = [binary_identity(args.source, item) for item in items.values()
                     if item["path"].endswith(".zip")]
    result = {
        "status": "UNRESOLVED; source catalog is not an input lock",
        "provider_commit": PROVIDER, "package_count": len(names),
        "package_list_source": index, "packages": packages, "support_files": support,
        "gitmodules": "not present in selected provider tree" if ".gitmodules" not in items else "see support_files",
        "verified_binary_inputs": binary_inputs,
        "candidate_mpv_static_named_reachability": static_closure(packages),
        "closure_limit": "lexical DEPENDS only; conditionals, generated variables, bootstrap and implicit dependencies remain unresolved",
    }
    with args.output.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(result, stream, indent=2, ensure_ascii=True)
        stream.write("\n")


if __name__ == "__main__":
    main()
