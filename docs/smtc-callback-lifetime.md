# Windows media callback lifetime: test-only design

The current Windows media-button handler captures its component directly.
If removal fails, the event source can retain that callback after component
destruction. The test fixture retains the actual registered delegate and checks
this condition without invoking the unsafe production delegate after teardown.
No production lifetime fix is included in this design.

The test-only model keeps admission state alive independently of the component.
The native delegate copies the button value, then queues it to a live GUI-thread
context while admission is locked. Closing clears the receiver under the same
lock before attempting event removal. A queued delivery checks admission again
and performs the action outside the lock. A retained old delegate therefore has
no ownership of the application component and cannot target a later registration.
The design does not hold its lock while calling media controls or playback code,
and the GUI does not wait for a callback to complete. Destruction and action
dispatch must run on the GUI thread; action dispatch must not continue to use a
receiver that synchronously destroys itself.

The fixture models a failed removal, a queued event closed before delivery,
reentrant invocation during removal, a callback held before enqueue, repeated
close, and an old delegate surviving a new registration. It is not evidence of
the safety of the current production handler. Existing native-control coverage
remains responsible for actual button mapping and playback behavior. The shared
model is Qt-portable; native Linux and macOS coverage remains required before a
cross-platform claim. Any production change requires separate review and proof.
