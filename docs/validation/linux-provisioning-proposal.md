# Linux runner provisioning proposal

**Proposed only; nothing has been installed or changed.** This is a reviewable
request based on the retained inventory from GitLab job 97369. It is not a
current host inventory or permission to act. An owner-controlled administrator,
not an unprivileged CI job, would perform any separately authorized operation.

The target is the designated Ubuntu amd64 runner. Verify the exact host and
manager against private InfraMap before installation. Captured mapping is not
live proof. No host was queried for this proposal.

The proposed runtime transaction is the captured simulation of
`apt-get --simulate --no-install-recommends install libmpv2=0.37.0-1ubuntu4`:
77 new packages, no upgrades or removals and no additional configure actions
beyond configuration of the proposed new packages.
The captured metadata estimates 58,192,150 download bytes and 164,030,464
installed bytes; these are package sums, not peak storage or rollback estimates.
No package indexes, repositories, holds, or installed package state are changed by
this proposal. The complete captured transaction, source artifact/member hashes,
candidate repository origins, package SHA-256 values, and byte counts are in
[linux-provisioning-transaction.json](linux-provisioning-transaction.json).

The separate job-owned development input is the exact Ubuntu amd64
`libmpv-dev` package `0.37.0-1ubuntu4`, 42,316 bytes, SHA-256
`c9cfd0b2f60e6377f0d339ef298a0772753ce7dc7310d250573a3ea9c0b9701e`,
captured at `pool/universe/m/mpv/libmpv-dev_0.37.0-1ubuntu4_amd64.deb`.
The proposal is to verify that artifact and extract it only into the job-owned
workspace; it does not authorize installing `libmpv-dev`, its full development
dependency closure, or globally refreshing package metadata. Qt remains a
separate job-owned dependency; macOS provisioning is separate.

Before any approved runtime change, the administrator must obtain fresh host
identity and package state, then compare a fresh solve against every captured
package, version, architecture, action, and configure action. Stop if anything
differs, including any existing package upgrade, removal, extra configure action,
or unavailable exact version. Verify origins, capacity and administrative
recovery arrangements before proceeding. A rollback requires an owner-reviewed
host snapshot or equivalent recovery plan: removing newly installed packages
blindly is not a reliable rollback and could affect shared dependencies or
package selection state.

## Runtime transaction proposed for separate approval

Each row is a **new install** from Ubuntu. `all` is an architecture-independent
package. Download and installed sizes are bytes from the captured package
metadata; repository origin and SHA-256 are recorded per row in the companion
transaction file.

| Package | Version | Arch | Action | Installed version | Origin | Download bytes | Installed bytes |
|---|---|---|---|---|---|---:|---:|
| libunibreak5 | 5.1-2build1 | amd64 | install | none | Ubuntu | 25012 | 153600 |
| libass9 | 1:0.17.1-2build1 | amd64 | install | none | Ubuntu | 103874 | 246784 |
| libva2 | 2.20.0-2ubuntu0.2 | amd64 | install | none | Ubuntu | 66362 | 224256 |
| libva-drm2 | 2.20.0-2ubuntu0.2 | amd64 | install | none | Ubuntu | 7124 | 46080 |
| libva-x11-2 | 2.20.0-2ubuntu0.2 | amd64 | install | none | Ubuntu | 12028 | 58368 |
| libvdpau1 | 1.5-2build1 | amd64 | install | none | Ubuntu | 27836 | 109568 |
| libvpl2 | 2023.3.0-1build1 | amd64 | install | none | Ubuntu | 99802 | 294912 |
| ocl-icd-libopencl1 | 2.3.2-1build1 | amd64 | install | none | Ubuntu | 38530 | 143360 |
| libavutil58 | 7:6.1.1-3ubuntu5 | amd64 | install | none | Ubuntu | 401184 | 1179648 |
| libcodec2-1.2 | 1.2.0-2build1 | amd64 | install | none | Ubuntu | 8998330 | 16857088 |
| libdav1d7 | 1.4.1-1build1 | amd64 | install | none | Ubuntu | 604018 | 1742848 |
| libgsm1 | 1.0.22-1build1 | amd64 | install | none | Ubuntu | 27810 | 83968 |
| libhwy1t64 | 1.0.7-8.1build1 | amd64 | install | none | Ubuntu | 584050 | 3423232 |
| libjxl0.7 | 0.7.0-10.2ubuntu6.1 | amd64 | install | none | Ubuntu | 1001370 | 3682304 |
| librav1e0 | 0.7.1-2 | amd64 | install | none | Ubuntu | 1021698 | 3314688 |
| libshine3 | 3.1.1-2build1 | amd64 | install | none | Ubuntu | 23170 | 60416 |
| libsnappy1v5 | 1.1.10-1build1 | amd64 | install | none | Ubuntu | 28634 | 91136 |
| libsvtav1enc1d1 | 1.7.0+dfsg-2build1 | amd64 | install | none | Ubuntu | 2424900 | 7218176 |
| libsoxr0 | 0.1.3-4build3 | amd64 | install | none | Ubuntu | 80038 | 214016 |
| libswresample4 | 7:6.1.1-3ubuntu5 | amd64 | install | none | Ubuntu | 63782 | 201728 |
| libx264-164 | 2:0.164.3108+git31e19f9-1 | amd64 | install | none | Ubuntu | 603814 | 1904640 |
| libx265-199 | 3.5-2build1 | amd64 | install | none | Ubuntu | 1226422 | 16484352 |
| libxvidcore4 | 2:1.3.7-1build1 | amd64 | install | none | Ubuntu | 219400 | 689152 |
| libzvbi-common | 0.2.42-2 | all | install | none | Ubuntu | 42360 | 164864 |
| libzvbi0t64 | 0.2.42-2 | amd64 | install | none | Ubuntu | 260558 | 705536 |
| libavcodec60 | 7:6.1.1-3ubuntu5 | amd64 | install | none | Ubuntu | 5851220 | 16626688 |
| libudfread0 | 1.1.2-1build1 | amd64 | install | none | Ubuntu | 18980 | 60416 |
| libbluray2 | 1:1.3.4-1build1 | amd64 | install | none | Ubuntu | 159390 | 396288 |
| libchromaprint1 | 1.5.1-5 | amd64 | install | none | Ubuntu | 30524 | 81920 |
| libgme0 | 0.6.3-7build1 | amd64 | install | none | Ubuntu | 134050 | 324608 |
| libopenmpt0t64 | 0.7.3-1.1build3 | amd64 | install | none | Ubuntu | 647356 | 1634304 |
| librabbitmq4 | 0.11.0-1ubuntu0.2 | amd64 | install | none | Ubuntu | 38306 | 113664 |
| libcjson1 | 1.7.17-1 | amd64 | install | none | Ubuntu | 24762 | 83968 |
| libmbedcrypto7t64 | 2.28.8-1 | amd64 | install | none | Ubuntu | 209262 | 540672 |
| librist4 | 0.2.10+dfsg-2 | amd64 | install | none | Ubuntu | 74894 | 191488 |
| libsrt1.5-gnutls | 1.5.3-1build2 | amd64 | install | none | Ubuntu | 316428 | 839680 |
| libssh-gcrypt-4 | 0.10.6-2ubuntu0.5 | amd64 | install | none | Ubuntu | 225808 | 627712 |
| libnorm1t64 | 1.5.9+dfsg-3.1build1 | amd64 | install | none | Ubuntu | 153694 | 397312 |
| libpgm-5.3-0t64 | 5.3.128~dfsg-2.1build1 | amd64 | install | none | Ubuntu | 166620 | 310272 |
| libzmq5 | 4.3.5-1build2 | amd64 | install | none | Ubuntu | 260300 | 714752 |
| libavformat60 | 7:6.1.1-3ubuntu5 | amd64 | install | none | Ubuntu | 1153444 | 2916352 |
| libbs2b0 | 3.1.0+dfsg-7build1 | amd64 | install | none | Ubuntu | 10556 | 40960 |
| libflite1 | 2.2-6build3 | amd64 | install | none | Ubuntu | 13601102 | 28182528 |
| libserd-0-0 | 0.32.2-1 | amd64 | install | none | Ubuntu | 43568 | 117760 |
| libzix-0-0 | 0.4.2-2build1 | amd64 | install | none | Ubuntu | 23570 | 72704 |
| libsord-0-0 | 0.16.16-2build1 | amd64 | install | none | Ubuntu | 15820 | 55296 |
| libsratom-0-0 | 0.6.16-1build1 | amd64 | install | none | Ubuntu | 17312 | 61440 |
| liblilv-0-0 | 0.24.22-1build1 | amd64 | install | none | Ubuntu | 40976 | 123904 |
| libmysofa1 | 1.3.2+dfsg-2ubuntu2 | amd64 | install | none | Ubuntu | 1157872 | 1297408 |
| libplacebo338 | 6.338.2-2build1 | amd64 | install | none | Ubuntu | 2654026 | 8867840 |
| libblas3 | 3.12.0-3build1.1 | amd64 | install | none | Ubuntu | 237978 | 705536 |
| libgfortran5 | 14.2.0-4ubuntu2~24.04.1 | amd64 | install | none | Ubuntu | 916316 | 3334144 |
| liblapack3 | 3.12.0-3build1.1 | amd64 | install | none | Ubuntu | 2646422 | 7297024 |
| libsphinxbase3t64 | 0.8+5prealpha+1-17build2 | amd64 | install | none | Ubuntu | 125618 | 325632 |
| libpocketsphinx3 | 0.8.0+real5prealpha+1-15ubuntu5 | amd64 | install | none | Ubuntu | 132898 | 313344 |
| libpostproc57 | 7:6.1.1-3ubuntu5 | amd64 | install | none | Ubuntu | 49884 | 152576 |
| libfftw3-double3 | 3.3.10-1ubuntu3 | amd64 | install | none | Ubuntu | 838254 | 2336768 |
| librubberband2 | 3.3.0+dfsg-2build1 | amd64 | install | none | Ubuntu | 129894 | 343040 |
| libswscale7 | 7:6.1.1-3ubuntu5 | amd64 | install | none | Ubuntu | 192938 | 705536 |
| libvidstab1.1 | 1.1.0-2build1 | amd64 | install | none | Ubuntu | 38550 | 94208 |
| libzimg2 | 3.0.5+ds1-1build1 | amd64 | install | none | Ubuntu | 254226 | 846848 |
| libavfilter9 | 7:6.1.1-3ubuntu5 | amd64 | install | none | Ubuntu | 4235172 | 13619200 |
| libdc1394-25 | 2.2.6-4build1 | amd64 | install | none | Ubuntu | 90084 | 257024 |
| libjack-jackd2-0 | 1.9.21~dfsg-3ubuntu3 | amd64 | install | none | Ubuntu | 288676 | 1158144 |
| libopenal-data | 1:1.23.1-4build1 | all | install | none | Ubuntu | 160710 | 262144 |
| libsndio7.0 | 1.9.0-0.3build3 | amd64 | install | none | Ubuntu | 29562 | 90112 |
| libopenal1 | 1:1.23.1-4build1 | amd64 | install | none | Ubuntu | 540202 | 1224704 |
| libsdl2-2.0-0 | 2.30.0+dfsg-1ubuntu3.1 | amd64 | install | none | Ubuntu | 685580 | 2030592 |
| libavdevice60 | 7:6.1.1-3ubuntu5 | amd64 | install | none | Ubuntu | 82320 | 276480 |
| libdvdread8t64 | 6.1.3-1.1build1 | amd64 | install | none | Ubuntu | 54676 | 157696 |
| libdvdnav4 | 6.1.1-3build1 | amd64 | install | none | Ubuntu | 39542 | 117760 |
| liblua5.2-0 | 5.2.4-3build2 | amd64 | install | none | Ubuntu | 123358 | 449536 |
| libmujs3 | 1.3.3-3build2 | amd64 | install | none | Ubuntu | 133844 | 375808 |
| libsixel1 | 1.10.3-3build1 | amd64 | install | none | Ubuntu | 108810 | 443392 |
| libva-wayland2 | 2.20.0-2ubuntu0.2 | amd64 | install | none | Ubuntu | 10906 | 58368 |
| libxpresent1 | 1.0.0-2build2 | amd64 | install | none | Ubuntu | 7380 | 34816 |
| libmpv2 | 0.37.0-1ubuntu4 | amd64 | install | none | Ubuntu | 1016404 | 3045376 |

## Display transaction: separate approval required

The captured display simulation requested `xvfb=2:21.1.12-1ubuntu1.9` and
`mesa-utils=9.0.0-2` and would add three packages and upgrade five installed
packages. It is **not included** in the runtime request. Its captured package
metadata sums are 4,684,554 download bytes and 12,199,936 installed bytes,
not peak usage. Do not substitute an older Xvfb to avoid the listed upgrades.

| Package | Version | Arch | Action | Installed version | Origin | Download bytes | Installed bytes |
|---|---|---|---|---|---|---:|---:|
| libpciaccess0 | 0.17-3ubuntu0.24.04.3 | amd64 | upgrade | 0.17-3ubuntu0.24.04.2 | Ubuntu | 19220 | 62464 |
| mesa-utils-bin | 9.0.0-2 | amd64 | install | none | Ubuntu | 164440 | 886784 |
| xserver-common | 2:21.1.12-1ubuntu1.9 | all | upgrade | 2:21.1.12-1ubuntu1.6 | Ubuntu | 35008 | 154624 |
| xserver-xephyr | 2:21.1.12-1ubuntu1.9 | amd64 | upgrade | 2:21.1.12-1ubuntu1.6 | Ubuntu | 1019030 | 2566144 |
| xserver-xorg-legacy | 2:21.1.12-1ubuntu1.9 | amd64 | upgrade | 2:21.1.12-1ubuntu1.6 | Ubuntu | 41968 | 186368 |
| xserver-xorg-core | 2:21.1.12-1ubuntu1.9 | amd64 | upgrade | 2:21.1.12-1ubuntu1.6 | Ubuntu | 1476450 | 3949568 |
| xvfb | 2:21.1.12-1ubuntu1.9 | amd64 | install | none | Ubuntu | 877038 | 2192384 |
| mesa-utils | 9.0.0-2 | amd64 | install | none | Ubuntu | 1051400 | 2201600 |

The inventory recorded no additional configure actions beyond those for the
proposed packages in either simulation.
A later approved display operation requires its own fresh transaction comparison
and approval; the blocked constrained simulation is not evidence that the display
packages can be installed without the five upgrades.
