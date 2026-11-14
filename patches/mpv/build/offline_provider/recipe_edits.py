"""Finite proposed substitutions against exact pinned provider source bytes."""

import hashlib

MESON = {
    "packages/rubberband.cmake": "988619a42e100de14d5c05cd65fd80b49b13334324726eb6634f56b7c4b05da0",
    "packages/zstd.cmake": "84c3e98c148ab52579c99106ac5f1256e842d3dd51369ad222c1d9f71fb716cb",
    "packages/lcms2.cmake": "78ac60adc47be993cf59f7f1d318d78558a2850755a8b4b5181f68434245edbe",
    "packages/opus.cmake": "2762c05c3e2cabf4f3959fdf60ea88bca522151be5940bd454cc5d3a106aa428",
    "packages/speex.cmake": "75a53b5a46f60bb1c09805c712fc8522e9ddeea55c02bdc033f07795d0541038",
    "packages/libdvdcss.cmake": "d04294b88d2ff8444ac8ce40227b34970903d72c855d4561db902d2b304825eb",
    "packages/libdvdread.cmake": "bc494f70ffccbc75e021def2c2f21d913161bdf2912a33d9efccd99516041676",
    "packages/libdvdnav.cmake": "e1295fb2df314fca53d7a92247ee11b6592c62b49cd28dd4a79224225a1e9619",
    "packages/fribidi.cmake": "9a220a21c07099ebc6391d828a677697c4f147a781fcb54f86e038dbbf620321",
    "packages/freetype2.cmake": "eed687f636ec335af2e341537e089857f75a659e69d02da5176715628bf3b035",
    "packages/libudfread.cmake": "0607b8034ca0feaebb4caff99cfca1ed1be8108c8c35b29b85a58ec0d872112a",
    "packages/libbluray.cmake": "0a5d301070c3239a673a1b7f360d6a14cf4f3c9b366c8a29a09bb463208ca71b",
    "packages/harfbuzz.cmake": "9b8a2f9f2b7dfb440ff7f6c925489ae1cee64b3119f5556f6c103c786f653f4b",
    "packages/fontconfig.cmake": "c20c10d75476bf1f99818e5dda7644c94ae157372b9fd396c09f1b2122b006aa",
    "packages/libass.cmake": "41ec0c08160e4d0a31b2d9dc214985056ad9b61d77530c526ee77b0acc9c84b6",
    "packages/dav1d.cmake": "c64418c6e46a296d414a4345eb8e7f61f68ae63490c573f6bcb7988e8b3782dc",
    "packages/highway.cmake": "26b46bc1f08f35d7fda6f2bab349224e39a3972942ccad7aca3b8b768954fa87",
    "packages/libplacebo.cmake": "64c407a343a43bfe8b372b12d5dac4621d1362f094239cfb2c215e6ce213332f",
    "packages/libva.cmake": "93ec04ce9d24844d548c69d20cc077c7c1a187901886d3956e021ed1b9aeb718",
    "packages/libpsl.cmake": "de81de4a2791976cc1d52c114f3861ceae2154f825b3dcb0e609e09163d8b56f",
    "packages/mpv.cmake": "cfb9b0fbaaf6013069fe3eeda7e9e9644546f09cd772dbd549f8eb7fa21c7122",
}
SUBRANDR = "cbf83815bc10a86c38151633826e413d34167410bc9569ee22c045c53a01f477"


def edit(path, payload):
    expected = MESON.get(path, SUBRANDR if path == "packages/subrandr.cmake" else None)
    if expected is None or hashlib.sha256(payload).hexdigest() != expected:
        raise ValueError("unknown recipe or source identity")
    if path in MESON:
        before = b"meson setup "
        after = b"meson setup --wrap-mode=nodownload "
    else:
        before = b"        cargo -Z unstable-options -C <SOURCE_DIR> xtask install"
        after = (b"        CARGO_NET_OFFLINE=true\n"
                 b"        cargo --locked --offline -Z unstable-options -C <SOURCE_DIR> xtask install")
    if payload.count(before) != 1:
        raise ValueError("unexpected recipe command count")
    return payload.replace(before, after, 1)
