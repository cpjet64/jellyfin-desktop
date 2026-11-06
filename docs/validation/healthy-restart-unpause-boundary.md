# Healthy restart unpause boundary: decision required

The fixture restore adapter currently arms the actual Player and dispatches a
paused load and seek. `PlayerComponent::dispatchNativeRestoreLoad` always loads
paused; `dispatchNativeRestoreSeek` validates the current native receipt and
intent. The adapter has no restoration unpause operation.

The existing `PlayerComponent::play()` resumes the ordinary playback intent,
changes its revision, and queues `set pause no` through the normal command
path. It returns no checked dispatch result. Calling it after restore would
change the revision in the restore key, so it cannot establish a checked,
current restoration outcome through the existing receipt and observation
checks. A wrapper around `play()` cannot supply that missing evidence.

A candidate is a fixture-only checked native restore unpause operation. It
would require the matching Grant to request playing, a current post-seek
observation, the same Player restore key and intent, and the same live
Grant/RestorePermit authority immediately before native dispatch. It would
report the actual dispatch result without replacing the restore key, and
cancellation or endpoint loss would revoke authority and require checked Stop
if loading began. The operation and its event/result evidence need explicit
design and boundary tests before implementation. The owner must serialize the
permit, cancellation and dispatch on the existing control thread; it cannot
claim that an in-transit cancellation has already reached the child.

Until that decision and implementation, restoration to a playing state is not
qualified. No native execution or CI allocation follows from this note.
