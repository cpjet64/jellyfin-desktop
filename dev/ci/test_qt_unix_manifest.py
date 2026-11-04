import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import unittest
from urllib.parse import urlparse


def validate_destination(value, target):
    if not isinstance(value, str) or not value or "\\" in value:
        raise ValueError("invalid extraction destination")
    parts = value.split("/")
    if (PurePosixPath(value).is_absolute() or PureWindowsPath(value).drive or
            any(part in ("", ".", "..") for part in parts)):
        raise ValueError("extraction destination must be a contained relative path")
    if value != target and not value.startswith(target + "/"):
        raise ValueError("extraction destination must stay within the Qt prefix")


class QtUnixManifestTest(unittest.TestCase):
    def test_locked_archives_and_package_totals(self):
        path = Path(__file__).with_name('qt-6.9.3-unix.json')
        data = json.loads(path.read_text(encoding='utf-8'))
        self.assertEqual(data['qt_version'], '6.9.3')
        self.assertEqual(set(data['platforms']), {'linux', 'macos'})
        expected = {
            'linux': ('linux_gcc_64', '6.9.3/gcc_64', 10),
            'macos': ('clang_64', '6.9.3/macos', 8),
        }
        for name, (suffix, target, count) in expected.items():
            with self.subTest(platform=name):
                platform = data['platforms'][name]
                self.assertEqual(platform['target_subdirectory'], target)
                self.assertEqual(platform['architecture'], 'x86_64')
                groups = platform['groups']
                self.assertEqual(len(groups), 4)
                self.assertEqual([g['package'] for g in groups], [
                    f'qt.qt6.693.{suffix}', f'qt.qt6.693.addons.qtwebchannel.{suffix}',
                    f'qt.qt6.693.addons.qtpositioning.{suffix}', f'extensions.qtwebengine.693.{suffix}',
                ])
                archives = [archive for group in groups for archive in group['archives']]
                self.assertEqual(len(archives), count)
                self.assertEqual(len({a['url'] for a in archives}), count)
                self.assertEqual(platform['archive_bytes_total'], sum(a['bytes'] for a in archives))
                self.assertEqual(platform['expanded_bytes_metadata_total'],
                                 sum(g['expanded_bytes_metadata'] for g in groups))
                for index, group in enumerate(groups):
                    self.assertEqual(group['version'], '6.9.3-0-202509261208' if index == 3 else
                                     '6.9.3-0-202509261207')
                    self.assertGreater(group['expanded_bytes_metadata'], 0)
                    self.assertEqual(urlparse(group['metadata_url']).netloc, 'download.qt.io')
                    for archive in group['archives']:
                        self.assertEqual(urlparse(archive['url']).netloc, 'download.qt.io')
                        self.assertIn('/' + group['package'] + '/' + group['version'], archive['url'])
                        self.assertEqual(archive['checksum_source_url'], archive['url'] + '.mirrorlist')
                        self.assertRegex(archive['sha256'], re.compile(r'^[a-f0-9]{64}$'))
                        self.assertGreater(archive['bytes'], 0)
                        destination = archive['extract_subdirectory']
                        validate_destination(destination, target)
                        is_linux_icu = name == 'linux' and '/6.9.3-0-202509261207icu-linux-' in archive['url']
                        self.assertEqual(destination, target + ('/lib' if is_linux_icu else ''))

    def test_rejects_unsafe_extraction_destinations(self):
        target = '6.9.3/gcc_64'
        for value in ('', '/tmp/qt', '../outside', target + '/../outside', 'C:/outside',
                      'C:outside', r'\\server\share', target + '//lib', target + '/./lib',
                      '6.9.3/gcc_64-other', None):
            with self.subTest(value=value), self.assertRaises(ValueError):
                validate_destination(value, target)
        validate_destination(target, target)
        validate_destination(target + '/lib', target)


if __name__ == '__main__':
    unittest.main()
