# Unfinished player session wiring

`player-session-wiring.patch` preserves an unfinished, source-only draft against
commit `16401e5e15095d35664828319173a286d52683e5`. Its SHA-256 is
`d6cf5ff04a00eef01100e86dd59d0316630fbab4d03aed759ed257b811738109`.
The patch is not applied to the application and does not enable recovery.

The draft captures a private load candidate and updates or invalidates it on
some user commands. Native load acceptance, position attribution, complete
intent/track handling, safe renderer lifetime, fresh-frame proof, and recovery
dispatch remain incomplete. The diagnostic source-copy pins and patched output
hashes have not been reconciled. Do not apply the patch as a completed feature;
review and validate it against the eventual renderer and event contracts first.
No build, test, application, or native recovery run was performed for this draft.
