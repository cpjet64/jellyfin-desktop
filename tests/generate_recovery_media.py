"""Generate deterministic, silent YUV4MPEG2 video for local recovery tests."""
import argparse
from pathlib import Path

WIDTH = 160
HEIGHT = 90
FPS = 30
FRAME_COUNT = 300
HEADER = f"YUV4MPEG2 W{WIDTH} H{HEIGHT} F{FPS}:1 Ip A1:1 C420jpeg\n".encode("ascii")
FRAME_HEADER = b"FRAME\n"
CHROMA = bytes([128]) * (WIDTH * HEIGHT // 2)


def luma_frame(index):
    """Encode a moving square and nine binary counter blocks in each frame."""
    pixels = bytearray([32]) * (WIDTH * HEIGHT)
    x = (index * 3) % (WIDTH - 20)
    y = 35
    for row in range(y, y + 20):
        pixels[row * WIDTH + x:row * WIDTH + x + 20] = bytes([220]) * 20
    for bit in range(9):
        value = 235 if index & (1 << bit) else 60
        left = 5 + bit * 16
        for row in range(8, 20):
            pixels[row * WIDTH + left:row * WIDTH + left + 12] = bytes([value]) * 12
    return pixels


def generate(destination):
    """Create a new clip; never overwrite an existing file."""
    with Path(destination).open('xb') as output:
        output.write(HEADER)
        for index in range(FRAME_COUNT):
            output.write(FRAME_HEADER)
            output.write(luma_frame(index))
            output.write(CHROMA)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path, help='new output .y4m file')
    args = parser.parse_args()
    generate(args.destination)


if __name__ == '__main__':
    main()
