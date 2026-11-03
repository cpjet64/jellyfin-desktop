"""Verify the generated recovery fixture independently of a video decoder."""
from pathlib import Path
import tempfile
import unittest

from tests import generate_recovery_media as media


class RecoveryMediaTest(unittest.TestCase):
    def test_clip_has_300_identifiable_moving_frames(self):
        build = Path(__file__).resolve().parents[1] / 'build'
        build.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='recovery-media-', dir=build) as directory:
            clip = Path(directory) / 'moving.y4m'
            media.generate(clip)
            with clip.open('rb') as source:
                self.assertEqual(source.readline(), b'YUV4MPEG2 W160 H90 F30:1 Ip A1:1 C420jpeg\n')
                previous = None
                for index in range(300):
                    self.assertEqual(source.readline(), b'FRAME\n')
                    frame = source.read(160 * 90 * 3 // 2)
                    self.assertEqual(len(frame), 21600)
                    counter = sum((frame[10 * 160 + 7 + bit * 16] > 150) << bit for bit in range(9))
                    self.assertEqual(counter, index)
                    self.assertEqual(frame[40 * 160 + ((index * 3) % 140) + 2], 220)
                    if previous is not None:
                        self.assertNotEqual(frame, previous)
                    previous = frame
                self.assertEqual(source.read(1), b'')
            with self.assertRaises(FileExistsError):
                media.generate(clip)


if __name__ == '__main__':
    unittest.main()
