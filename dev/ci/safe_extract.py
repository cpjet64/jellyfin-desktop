"""Extract pinned tool archives to a new, bounded job-owned directory."""

from pathlib import Path, PurePosixPath
import shutil
import stat
import sys
import tarfile
import zipfile

MAX_BYTES = 1024 * 1024 * 1024


def target_path(root, name):
    path = PurePosixPath(name)
    if path.is_absolute() or not path.parts or any(part in ("..", "") or ":" in part for part in path.parts):
        raise ValueError(f"Unsafe archive path: {name}")
    target = root.joinpath(*path.parts)
    if root not in target.resolve().parents:
        raise ValueError(f"Archive path escapes destination: {name}")
    return target


def write_file(root, name, source, size, executable):
    target = target_path(root, name)
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open("xb") as output:
        shutil.copyfileobj(source, output, length=1024 * 1024)
    if target.stat().st_size != size:
        raise ValueError(f"Archive size mismatch: {name}")
    if executable:
        target.chmod(target.stat().st_mode | stat.S_IXUSR)


def extract_tar(path, root, only=None):
    total = 0
    with tarfile.open(path, "r:*") as archive:
        for member in archive:
            if only is not None and member.name != only:
                continue
            target = target_path(root, member.name)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                total += member.size
                if total > MAX_BYTES:
                    raise ValueError("Archive expansion limit exceeded")
                with archive.extractfile(member) as source:
                    write_file(root, member.name, source, member.size, member.mode & 0o111)
            else:
                raise ValueError(f"Archive link or special entry rejected: {member.name}")
    if only is not None and not (root / only).is_file():
        raise ValueError(f"Required archive member not found: {only}")


def extract_zip(path, root):
    total = 0
    with zipfile.ZipFile(path) as archive:
        for member in archive.infolist():
            mode = member.external_attr >> 16
            if stat.S_ISLNK(mode):
                raise ValueError(f"Archive link rejected: {member.filename}")
            kind = stat.S_IFMT(mode)
            if kind not in (0, stat.S_IFREG, stat.S_IFDIR):
                raise ValueError(f"Archive special entry rejected: {member.filename}")
            target = target_path(root, member.filename)
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            total += member.file_size
            if total > MAX_BYTES:
                raise ValueError("Archive expansion limit exceeded")
            with archive.open(member) as source:
                write_file(root, member.filename, source, member.file_size, mode & 0o111)


def main():
    if len(sys.argv) not in (3, 4):
        raise ValueError("usage: safe_extract.py ARCHIVE NEW_DIRECTORY [TAR_MEMBER]")
    archive = Path(sys.argv[1]).resolve(strict=True)
    root = Path(sys.argv[2]).resolve()
    root.mkdir(parents=True, exist_ok=False)
    if zipfile.is_zipfile(archive):
        if len(sys.argv) != 3:
            raise ValueError("Selected members are only supported for tar archives")
        extract_zip(archive, root)
    else:
        extract_tar(archive, root, sys.argv[3] if len(sys.argv) == 4 else None)


if __name__ == "__main__":
    main()
