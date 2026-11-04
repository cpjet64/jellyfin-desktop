# Intel macOS media feature and evidence matrix

This is a proposed acceptance matrix, not evidence of an installed build.
The reference is Homebrew core commit
`c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5`; the source lock is incomplete.
The official Qt 6.9.3 prefix is an independent input. The selected SDK,
deployment target, Xcode and compiler compatibility remain unverified.

| Capability | Reference and Desktop requirement | Required evidence |
| --- | --- | --- |
| Embedded rendering | mpv enables libmpv; Desktop selects `vo=libmpv` | Real Qt application video, start/stop/restart/close and event lifecycle; CLI alone is insufficient |
| Hardware decode | FFmpeg enables VideoToolbox and AudioToolbox; Desktop exposes auto, copy, disabled and macOS compatibility modes | Configuration plus observed H.264, HEVC and AV1 decode/interoperability where supported; distinguish actual hardware from software fallback |
| Audio | Desktop selects device, channels, exclusive mode and passthrough codecs | Audible output, switching and fallback; exercise AC3/DTS/EAC3/DTS-HD/TrueHD passthrough only with suitable hardware; audio-device change regression for mpv patch |
| Subtitles | libass, shaping dependencies and CoreText on macOS | Styled ASS, complex shaping, font lookup, external/embedded selection, delay and off |
| Filters | Desktop may request AC3 processing and other filters | Verify `libavfilter` and AC3 encoder/filter availability by runtime capability query and representative behavior, fail if missing |
| Codecs | Reference enables dav1d, VPX, Opus, LAME, x264, x265, SVT-AV1 and vmaf | Compare resolved configure flags and codec capabilities to required reference set; exercise representative media |
| Network | FFmpeg enables OpenSSL; Desktop loads media URLs | HTTPS success with trusted certificate, rejection of invalid certificate, streaming/seek/recovery against controlled endpoint |
| Color and scaling | libplacebo enables shaderc, Vulkan and lcms; mpv enables Vulkan and MoltenVK | Verify built features and selected runtime path; test scaling/color and HDR where supported; do not assume embedded rendering uses Vulkan |
| Playback control | Desktop manages tracks and playback via libmpv | Seek, play/pause, buffering, end/error, server transcode, subtitle/audio switching, manual overrides |

Desktop sets `ytdl=false` by default, but supports manual mpv settings. Any
exclusion of yt-dlp, scripting, VapourSynth, bluray or other formula features
must state its effect on those settings and receive product review. Documentation
generation may be excluded only after proving no runtime/resource dependency;
the same applies to standalone CLI tools. Reference formula dependencies and
flags are not proof of built capabilities. Every required optional dependency
must be explicitly enabled and capability-checked; missing input is a failure.

For every binary and dylib, inspect the x86_64 slice, minimum OS, install name,
RPATHs and recursively resolved non-system dependencies. Validate header,
pkg-config and CMake discovery using only the job-owned prefix and approved SDK.
Disallow implicit subproject fetches and host-prefix fallback. Record separate
compressed, expanded, temporary-build, installed and evidence sizes; all
remain unknown. A build-only pass is not native graphics or desktop acceptance.
