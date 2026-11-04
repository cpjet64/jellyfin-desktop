# Synthetic restore Debug attempt, 2026-11-04

The single Windows Debug synthetic restore attempt failed before fault injection and before the G2 portion of the scenario. The retained QtTest log ends at the failed replay-recipe capture assertion and has no complete QtTest footer. It does not establish the cause of the failed capture or any recovery behavior. No Release run or retry was performed.

The binary was built from `041cb08bfe56072f2e085d1f46243dace6da7da9`; the orchestration source was `95415700b6740c626d58d7f6da31a371d21ca13c`. The log records playback reaching the playing state before `recipe.capture` returned false. It does not retain the raw property statuses and values needed to identify which capture predicate failed. Two pre-fault frame images were retained, but they do not prove the replay recipe was captured.

The application exited with code 1. The desktop helper returned 7 and reported zero job processes before and after cleanup, with no termination request or forced cleanup. The runner's subsequent missing-success-marker error reflects that failed helper outcome; it is not a separate diagnosis of the capture failure.

The retained private evidence has these SHA-256 identities:

| Evidence | SHA-256 |
| --- | --- |
| Debug executable | `5c782ecb3c7d59808ec31282960898ad827efe0c7773d82cf6a239f4cee929c8` |
| Desktop helper | `706e0a939b4dff4b2f2996996863943c3d3a5dfba6dc46e96578f4fb73bd7a42` |
| QtTest log | `609613774e3c4fac6cb77b5d31d2c71ed8e34502d88287f29c1d86bb7d9035b5` |
| Helper stdout | `e51ee4868dc71217dd9719acb54053e87569d6d6d4654a4e6993502b3194eb7d` |
| Helper stderr, empty | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| Runner observation | `2169cf230c9f772d00759c4076c3cdb75cd40ddf663e55041b1da32b22408eaa` |
| Launch log | `248b7157681f90a3626e92a65b3d28f682a7d1a0f4c9f770a2ec01d21cf8205c` |

A bounded next diagnostic is to add source-level capture-predicate telemetry for the task-owned test clip: record the relevant raw mpv status and observed path form, entry, time and pause values at the capture attempt. The log shows the clip opened using Windows backslashes, while the source compares path values with a canonical path; a path-form mismatch is plausible but the captured property value was not recorded. If confirmed, any correction needs cross-platform path regression coverage. This proposal does not assume a fix or authorize another native run.
