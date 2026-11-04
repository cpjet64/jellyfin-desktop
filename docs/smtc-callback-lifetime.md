# Windows media callback lifetime

The production Windows media-button delegate captures shared admission state,
not the component. If removal fails, the event source can retain the delegate;
the closed admission state prevents it from reaching the destroyed component.
The fixture retains and invokes the actual registered delegate after teardown.

The admission state remains alive independently of the component.
The native delegate copies the button value, then queues it to a live GUI-thread
context while admission is locked. Closing clears the receiver under the same
lock before attempting event removal. A queued delivery checks admission again
and performs the action outside the lock. A retained old delegate therefore has
no ownership of the application component and cannot target a later registration.
The design does not hold its lock while calling media controls or playback code,
and the GUI does not wait for a callback to complete. Destruction and action
dispatch must run on the GUI thread; action dispatch must not continue to use a
receiver that synchronously destroys itself.

The fixture exercises a failed removal, a queued event closed before delivery,
reentrant invocation during removal, a callback held before enqueue, repeated
close, and an old delegate surviving a new registration. Existing native-control coverage
remains responsible for actual button mapping and playback behavior. The shared
admission helper has a platform-neutral QtTest target; Windows execution does
not establish native Linux or macOS coverage, which remains required. These
tests do not establish operating-system event dispatch or successful revocation
when the operating system reports removal failure.
