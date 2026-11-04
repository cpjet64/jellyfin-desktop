"""Bound and replay every verified archive header before SDK publication."""

from contextlib import contextmanager
from dataclasses import replace

from linux_qt_reader import Reader
from qt_sdk_archive import plan_entries


MAX_ENTRIES = 250000
MAX_METADATA = 64 * 1024 * 1024
MAX_FILE = 2 * 1024 * 1024 * 1024
MAX_EXPANSION = 4 * 1024 * 1024 * 1024


def scan_archives(archives, library, sdk_root):
    groups, total_entries, metadata_bytes = [], 0, 0
    for item, path in archives:
        entries = []
        with Reader(path, library, item) as reader:
            while (entry := reader.next_entry()) is not None:
                total_entries += 1
                metadata_bytes += len(entry.path.encode("utf-8")) + len(entry.link.encode("utf-8"))
                if (total_entries > MAX_ENTRIES or metadata_bytes > MAX_METADATA or
                        len(entry.path.encode("utf-8")) > 4096 or len(entry.link.encode("utf-8")) > 4096 or
                        entry.size > MAX_FILE):
                    raise ValueError("Qt archive member or metadata limit exceeded")
                entries.append(entry)
        groups.append((item["extract_subdirectory"], entries))
    placeholders = [(dest, [replace(e, open_data=lambda: None) if e.kind == "file" else e
                            for e in entries]) for dest, entries in groups]
    plan_entries(placeholders, MAX_EXPANSION, sdk_root)
    return groups


class Replay:
    def __init__(self, reader, expected):
        self.reader, self.expected, self.index = reader, expected, 0

    def advance(self):
        actual = self.reader.next_entry()
        if self.index >= len(self.expected) or actual != self.expected[self.index]:
            raise ValueError("archive metadata changed between passes")
        self.index += 1
        return actual

    @contextmanager
    def open_file(self, index):
        while self.index <= index:
            entry = self.advance()
            if entry.kind == "file" and self.index - 1 != index:
                raise ValueError("archive replay skipped file payload")
        yield self.reader

    def finish(self):
        while self.index < len(self.expected):
            if self.advance().kind == "file":
                raise ValueError("archive replay skipped file payload")
        if self.reader.next_entry() is not None:
            raise ValueError("archive gained an entry between passes")


def attach_replays(groups, readers):
    output, replays = [], []
    for (destination, entries), reader in zip(groups, readers, strict=True):
        replay = Replay(reader, entries)
        replays.append(replay)
        output.append((destination, [replace(entry, open_data=lambda i=index, r=replay: r.open_file(i))
                                     if entry.kind == "file" else entry
                                     for index, entry in enumerate(entries)]))
    return output, replays
