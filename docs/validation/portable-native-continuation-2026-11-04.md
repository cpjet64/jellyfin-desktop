# macOS portable test continuation, 2026-11-04

Two standalone native macOS runs continued the portable checks described in [the earlier checkpoint](portable-native-2026-11-04.md). These results cover selected Python test suites, not a complete pipeline, SDK, graphics, GUI, or product acceptance. The earlier failed attempt remains part of the record.

## First continuation

At source `d9fc136cbba87149d33c03dfc4f315b7cb5ed48e`, the first five suites passed 39 tests with no skips. The sixth suite, `test_linux_qt_pc.py`, passed eight tests and reported one error; the remaining four suites were not run. Its failing test required a tracked inventory JSON file that the standalone source bundle omitted. This was a bundle packaging error, not a demonstrated product failure. The attempt was not rerun unchanged.

The original failure log is retained privately; its SHA-256 is `0ca059e96568c16b81c8d8f6894e18e5f1df318b7d7043cef2ccd79c7c83777b`. The terminal receipt SHA-256 is `a7eed116f135b5ede983b86c9961b89128a4bba67830601416a3a97f04407cf4`. The original source bundle SHA-256 is `b88e9dba22bac0011e9c3c415ff97584e40d9a7efcc389618801a4b3ab994686`.

## Corrected continuation

The corrected bundle included the missing inventory and was verified against the Git objects for source `57c1ddea86201ade333470c68a4aa1b0f1bb9964`. The six selected suites passed on native macOS with Python 3.13.15:

| Suite | Passed | Skipped |
| --- | ---: | ---: |
| `test_linux_qt_pc.py` | 9 | 0 |
| `test_mac_helpers.py` | 7 | 0 |
| `test_mac_helpers_acquire.py` | 6 | 0 |
| `test_acceptance.py` | 20 | 0 |
| `test_native_order.py` | 10 | 0 |
| `test_mac_carchive.py` | 13 | 0 |
| **Total** | **65** | **0** |

The corrected bundle SHA-256 is `cd12f4d045be3a26adb3548bef0158ad59da388b7a98006769feddc506b07782`; its terminal receipt SHA-256 is `b5d8cbb9275e50b9c054d36303b977abdea5ca30e8d307d996c54194e7e33206`. The receipt records the individual private log hashes. The process supervisor checked that each owned group had settled before returning; this does not establish natural supervisor exit or containment of a deliberately escaped session.

The first five passing suites belong to the earlier source checkpoint, while the corrected six belong to the later checkpoint. They are not a single full run at one revision.
