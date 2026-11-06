# Native playback receipt foundation

The private fixture seam records submission, command reply, and matched
playlist-entry START and END separately. It uses the existing Player event
consumer only in the patched external-events configuration; the system mode
returns unavailable. A nonzero host/core/revision/attempt key and a bounded
event buffer limit correlation. Event queue overflow, missing identity,
redirect, conflicting START, shutdown, or invalidation prevents qualification.

The private native observation uses one fixed asynchronous `expand-text`
command and the existing event drain. Its bounded reply must identify the
single matched entry, expected pause, playing/current state, and neither idle
nor seeking, with a finite position within the generated fixture's 10 seconds.
Only the request-to-receipt interval is recorded; it is not a sample timestamp.
The separate injected position seam remains test evidence, not a native sample.
The provisional web bridge cannot invoke these methods. Selected binaries'
event and reply semantics are not qualified by source-only checks.

Checkpoint consumption burns its one-use permission before comparing canonical
bounded codec output to the current retained receipt and keyed fixture observation.
The private restoration controls can arm a fresh B intent, submit its fixture
load paused, and issue an exact seek after B's matched load and paused native
observation. A seek reply and subsequent playback restart are recorded; a fresh
post-seek observation is also required. No qualified decoded-frame callback,
unpause path, owned-channel Player adapter, or cross-process restoration is
implemented here. Position alone does not prove seek completion. Automatic
recovery remains unavailable. No URL, file path, headers, or options are serialized.
