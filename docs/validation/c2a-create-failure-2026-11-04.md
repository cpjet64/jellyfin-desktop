# C2a create-failure diagnostic: Windows

The diagnostic create-failure fixture passed in separately contained Windows
Debug and Release processes, built from source revision
`8207e1a4ca3ebcbe9ee317a79e72806b317b00d1` with Qt 6.9.3 and MSVC 2022.
Both runs reported three QTest passes, zero failures and zero skips, with a
complete footer. This is the first of five C2a cases; it is not C2a acceptance
or proof of playback recovery. Linux and macOS results remain outstanding.

| Evidence | Debug SHA-256 | Release SHA-256 |
| --- | --- | --- |
| Fixture executable | `c2abcbe470c7c5fd3ae1bee1fb2edae1e08f8ec31e55063b6c90ac70222daaed` | `2a2479ae097f52b3ba27cbe763287885b34f8a8adda944cf5558fb26343e9239` |
| QTest log | `27b0d60abc6c5543391c398c86cad80f0557cc6741ddd983a63f5362367f4855` | `4a3d822f4988c033e375fef9bde2099a3d529f6ca46e812558bfae7036e625d3` |
| Helper stdout | `385d326fbaa7f918bbda548f327e2a9d4d9963ddeea9a2e9c3cbd0084180fd9a` | `09af7de08b139da4d4b07af67016ea2454a66df0ddcc01510a3e5c03a82e3a53` |
| Lifecycle JSON | `da87788e46bac0139d2d99a7a8f419d14cfd03d113fb3cabe3fa4f7b6839aed0` | `8666ef856b54a12845707b919471beb94c1a9ac24fcd2981959f8487b487507d` |

Both helper stderr files were empty (SHA-256
`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`).
Each helper observed a natural process exit of zero, a known empty job before
and after cleanup, and no termination request.

The fixture observed one typed create-stage failure for generation 1 with raw
error -18, and verified that the injected error was consumed. Each run recorded
zero ready signals, Player binding, callbacks, frees and retirement ACKs. The
create injection bypasses the real mpv render-context creation call, so this
case deliberately does not bind or load Player through a pretend-ready item.

The frame pump acknowledged its pause on the render worker with a current
context and no unfinished frame. The lifecycle records report one Qt cleanup,
cleared wrappers and aliases, detached fallback surface, destroyed control and
context, a joined worker, and a destroyed surface. In both records cleanup ACK,
worker join and surface destruction have the respective order values 6, 7
and 8. The fixture checks worker affinity; these counters do not by themselves
establish playback or a successful retirement.
