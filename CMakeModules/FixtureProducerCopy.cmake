# Fixture-only source copies. Inputs and patched outputs are pinned separately.
if(NOT PLAYER_INTENT_DIAGNOSTIC_ONLY OR NOT USE_STATIC_MPVQT OR MPVQT_EXTERNAL_EVENTS OR MPVQT_C2_DIAGNOSTIC_ONLY)
  message(FATAL_ERROR "Fixture producer copies require diagnostic-only static MpvQt, external events OFF, C2 OFF")
endif()
find_package(Git REQUIRED)
set(_fixture_copy "${CMAKE_BINARY_DIR}/fixture-producer-copy")
if(IS_SYMLINK "${_fixture_copy}")
  message(FATAL_ERROR "Fixture copy root must not be a link")
endif()
if(EXISTS "${_fixture_copy}")
  if(NOT EXISTS "${_fixture_copy}/.fixture-producer-owned" OR IS_SYMLINK "${_fixture_copy}/.fixture-producer-owned")
    message(FATAL_ERROR "Fixture copy root is not owned by this generator")
  endif()
  file(READ "${_fixture_copy}/.fixture-producer-owned" _fixture_owner)
  if(NOT _fixture_owner STREQUAL "fixture-producer-copy-v1\n")
    message(FATAL_ERROR "Fixture copy ownership marker mismatch")
  endif()
endif()
foreach(folder IN ITEMS mpvqt player tests core ui input power)
  if(IS_SYMLINK "${_fixture_copy}/${folder}")
    message(FATAL_ERROR "Fixture copy directory must not be a link: ${folder}")
  endif()
endforeach()
set(_fixture_mpv_inputs
  "mpvabstractitem.cpp|1cd2b083e531da9d67465bda282f1ea085fb01281bfa26a6a4e2451809ff2de0"
  "mpvabstractitem.h|d166944d4ca34a9f9499b0ff00c89c0216f10760022293833883741b7e156a27"
  "mpvabstractitem_p.h|93e06e780333507d559cad386d07ad9bf734bc9cce24f8ef70dacb1844582fa6"
  "mpvcontroller.cpp|0c2d0d9c091cee55c497f63fc13b00eead20767eca2f75575b601d471e0fe999"
  "mpvcontroller.h|eede2d838a167de76fe801804dcb3c6cc9094b02c437a9b551c6d0802bb474a8"
  "mpvcontroller_p.h|0ce2f019dec773cca79fc2bcd3f45761793f5150a2e1290963ace2d85ec3daed"
  "mpvrenderer.cpp|b519e71867813e8bcb73ef4353df1d32700e0ca988575c1e2f4017c1d1d1b798"
  "mpvrenderer.h|a35f52d0d4641db2c4bb03d0ced7ba40864f6e6b471b2cb42cc3e7068dda919f")
set(_fixture_player_inputs
  "AlbumArtProvider.cpp|39b727b75eea6ae34e52803eaa90a1f8a769c19357e2ed88a005f076c7fc2c56"
  "AlbumArtProvider.h|87893ba2a57843fc402f8bbe418c533a2e283c67d1ccf682bc63304d89205eac"
  "MpvVideoItem.cpp|5d738fb243f8e3d23e245d78965c6440b96f32d1e78cb405e77c341b99575b06"
  "MpvVideoItem.h|1e1bba8c18f5548535bc8e71da1259457a081cab8cda3dd8957c10852b8b8ab2"
  "OpenGLDetect.cpp|8dc58b06d131a2a7416189405145735c3fdb0d7f6632d31285862b524ee2b77c"
  "OpenGLDetect.h|576ecf842f1cc1b4d0aabef1bc87e060a057d23015a0ca39be7f96108e881970"
  "PlayerComponent.cpp|febe98eb5babf108f5ad53542ad99b3b3e207ada13a2f36432235f9692cc2b7c"
  "PlayerComponent.h|047877c4c057926643a89356b2ee0facd7898ec29d06f839460139d29705d75b"
  "PlayerComponentCommands.cpp|407656d9a30cae0e9c1380e80c79903ade593c8bfba53e2e59c62cf06e987825"
  "PlayerIntentCommands.cpp|36a8572d536a22e5aa35caa7a8d3a8c736c5b7471535c209232a1dfb986f4e9b"
  "PlaybackNativeReceipts.cpp|aded68382fd4c6dcd255c8c4c4b01a82cf1bee9e367fd5f2a71f244f788fa414"
  "PlaybackNativeReceipts.h|c397d371de1bd60fb923cb5f5c2291d284c901b0b4236207b22b40b5131b321f"
  "PlayerNativeReceipts.cpp|faad896cf062840fe61e019285989ac25ab3ad0d258541c2c694d8a9ee1139ef"
  "NativePlaybackObservation.cpp|042ab0b147803502797e9264e5c5400e42469da77381b8fb16172a56f6d44447"
  "NativePlaybackObservation.h|6bbef23c9b2273d01f8a6298ce81fc49a7e17673a43e1f0a234d85ccfcc394ed"
  "PlayerNativeObservation.cpp|6784a8284ba18ca864316c2ccba9e8ee90c8e4f7dcdc1e499e8f5446b511ceb3"
  "PlayerNativeRestore.cpp|ed3f1a9aac0ca6b2c00795f5908f44e2213de7a9fa8548dd32db7c3d27052ccb"
  "PlaybackIntent.cpp|fc7ac3efaa2015c42420deea8c657b8db86a5795f4f7383785c3113ba64349fe"
  "PlaybackIntent.h|ccefa6ddf3be574bc2580af21b59567e49c3fbe5c68a0634a9426fa02c5f31b3"
  "PlaybackCheckpoint.cpp|ad66683598a4f46956e5f4711fbf580faf0828057be39fe5ed055b9c327f8f52"
  "PlaybackCheckpoint.h|f845a4070b623c29ff26bc3335f95910d31029878eac8de40870f0b57166fe5b"
  "PlayerIntentObservation.h|dc2bf8491cb58d2b30156245d18c8590a3ebebbfe3932c9bb1df97c1bdaebe31"
  "PlayerMpvEvents.cpp|3d2c55d910485978242fa2348d54f28ef7a74cd299073ab6843aaa1b2dbd0d55"
  "PlayerReleasedState.cpp|82a125b3629891080ec4e051841ee309d7c967374c66532a6928e3664437f62e"
  "QtHelper.h|87fbb5a65a740998e9da037f1527906b214d90f758586538d1395237ac4a388a"
  "WindowsLoaderMode.h|4458f4d3fbb9d7bcef5342a9c22bf440e1a462256084bb66202d28c4924e7fe3"
  "WindowsMpvLoader.cpp|d26f12893901f4b2896b7c92f4134877926d5a4821fc0367a7c327e52158fc8a")
set(_fixture_test_inputs
  "test_webclient_containment.cpp|75de059742269bc205841cc2026114f5eb0cf6a478caade681a3a8ff07f8486b")
set(_fixture_capture_inputs
  "frame_acceptance.h|bfe7bf1d7506dede2a1279b8138afac06a14a1d65758ed5bbf7e28a8bef67db2"
  "frame_acceptance.cpp|268f2e813440cce50be70d269031fa180dcd6fbe320f5c80b014fd88aab16231"
  "frame_capture.h|582f3bbb462fe994df07709d4b86d49107387edfab150c2ab8c79ce27615d66c"
  "frame_capture.cpp|f0f0c44b9f66282c5314356d10b49325086bfd003b94b39b432c3f792654db0a")
set(_fixture_core_inputs "ComponentManager.cpp|e69695ac1e88b8044793ccd5245c61a156f483db5d30c74ef284627f701eae66")
set(_fixture_ui_inputs "WindowManager.cpp|d89e641bad2aa01bf0e2e2855e345a1b155ba73efbe32e30ba708747c1073ae7")
set(_fixture_input_inputs "InputComponent.cpp|1d8814277c3b901455cfcf25fc31400c78b4be8bcde9e36c09bc624ac7a9b823")
set(_fixture_power_inputs "PowerComponent.cpp|0d1c354a121addab787d2390c586b1b762754534eee98f168a39aff854c5b3f5")
set(_fixture_output_hashes
  "core/ComponentManager.cpp|c9a15173b349ce7b54f97d9e6a42188b5e4a337128bf4503f2c718a266ed0725"
  "ui/WindowManager.cpp|448aa4969ba5a0b5808ebbee82dc83543c6e0a9578e00145b791af49d664cf91"
  "input/InputComponent.cpp|f9eb0ed51f3fb3e841d8afda27bc6d950a2d30020297fc74d815f7a0585165dd"
  "power/PowerComponent.cpp|b9f34dc28a2e3ff0b475cdfbbb8840284a8c80012458a0854079ae8cd9fccdda"
  "mpvqt/mpvabstractitem.cpp|d8f6143b541b18474a383723d761f8ce0d0f3d7b0bc95d52d19ed3862f2f145f"
  "mpvqt/mpvabstractitem.h|1f6cef7948680f6d5923b721ba9ed471351ccff9099f66fc708ffff7a5c73613"
  "mpvqt/mpvcontroller.cpp|46c3634917f6cdd749da39343512d21c9746dfe5c1383006c76f02b418e631d0"
  "mpvqt/mpvcontroller.h|02d4b3499ec99d3eb0f3d5e5f7bc7e9d283fb027a72a195c2779876e5f5229c7"
  "mpvqt/mpvrenderer.cpp|ee623a28c4bb694f85280d32708f54d7426a0a3c475deb73c8f97f6c5ce9d90d"
  "player/MpvVideoItem.cpp|df2d8f3429067ef94cf4e737bad91555ff3d47545c6a03502a6763267895c2f8"
  "player/MpvVideoItem.h|697c7c2d34cdf99c266dcab77623a9be5c35ab966cb555739932159e8bed13cf"
  "player/PlayerMpvEvents.cpp|4640cd20afb7539adf4fb5420273f871ce38aead8d64c4f393d8a32b7d02215f"
  "player/PlayerComponent.cpp|1bac5bf2562fe2d56b0936d28182d0fe6fcc72697b139608709f9061a3fdf400"
  "player/PlayerComponent.h|9c5ba8ecc6ba9f0b8e1be8772e4a80d383cccdd494f8c3d31d852ff2223dbc3f"
  "tests/test_webclient_containment.cpp|c8a607dcea1de26ebfaf7eb3668814cf7c2605940fcb29b86eda827c31e55bf6")

function(_fixture_copy_inputs source destination list_name)
  file(MAKE_DIRECTORY "${destination}")
  foreach(entry IN LISTS ${list_name})
    string(REPLACE "|" ";" pair "${entry}")
    list(GET pair 0 name)
    list(GET pair 1 expected)
    set(path "${source}/${name}")
    if(NOT EXISTS "${path}" OR IS_SYMLINK "${path}")
      message(FATAL_ERROR "Fixture input missing or linked: ${path}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(READ "${path}" contents)
    string(REPLACE "\r\n" "\n" contents "${contents}")
    string(SHA256 actual "${contents}")
    if(NOT actual STREQUAL expected)
      message(FATAL_ERROR "Fixture input hash mismatch: ${path}")
    endif()
    if(IS_SYMLINK "${destination}/${name}")
      message(FATAL_ERROR "Fixture output must not be a link: ${destination}/${name}")
    endif()
    file(WRITE "${destination}/${name}" "${contents}")
  endforeach()
endfunction()

_fixture_copy_inputs("${CMAKE_SOURCE_DIR}/external/mpvqt/src" "${_fixture_copy}/mpvqt" _fixture_mpv_inputs)
_fixture_copy_inputs("${CMAKE_SOURCE_DIR}/src/player" "${_fixture_copy}/player" _fixture_player_inputs)
_fixture_copy_inputs("${CMAKE_SOURCE_DIR}/tests" "${_fixture_copy}/tests" _fixture_test_inputs)
_fixture_copy_inputs("${CMAKE_SOURCE_DIR}/tests/fixture_restart" "${_fixture_copy}/mpvqt" _fixture_capture_inputs)
foreach(group IN ITEMS core ui input power)
  _fixture_copy_inputs("${CMAKE_SOURCE_DIR}/src/${group}" "${_fixture_copy}/${group}" _fixture_${group}_inputs)
endforeach()
set(_fixture_patch "${CMAKE_SOURCE_DIR}/patches/fixture-producer/0001-producer-binding.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_fixture_patch}")
file(READ "${_fixture_patch}" _fixture_patch_contents)
string(REPLACE "\r\n" "\n" _fixture_patch_contents "${_fixture_patch_contents}")
string(SHA256 _fixture_patch_hash "${_fixture_patch_contents}")
if(NOT _fixture_patch_hash STREQUAL "4eb132cc3598d22e57676e184b456a8338ce514a738b529d266616c585d9ec81")
  message(FATAL_ERROR "Fixture patch hash mismatch")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
  "${GIT_EXECUTABLE}" apply --no-index --unidiff-zero --whitespace=nowarn "${_fixture_patch}"
  WORKING_DIRECTORY "${_fixture_copy}" RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Fixture source patch failed: ${error}")
endif()
set(_renderer_patch "${CMAKE_SOURCE_DIR}/patches/fixture-producer/0002-render-capture.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_renderer_patch}")
if(NOT EXISTS "${_renderer_patch}" OR IS_SYMLINK "${_renderer_patch}")
  message(FATAL_ERROR "Fixture renderer patch missing or linked")
endif()
file(READ "${_renderer_patch}" _renderer_patch_contents)
string(REPLACE "\r\n" "\n" _renderer_patch_contents "${_renderer_patch_contents}")
string(SHA256 _renderer_patch_hash "${_renderer_patch_contents}")
if(NOT _renderer_patch_hash STREQUAL "cb95ccdcf16a887fa0ad53b340d1be3d0b8b861cf313b65f39d41b30b43ba7da")
  message(FATAL_ERROR "Fixture renderer patch hash mismatch")
endif()
set(_renderer_patch_copy "${_fixture_copy}/.renderer-patch")
if(IS_SYMLINK "${_renderer_patch_copy}")
  message(FATAL_ERROR "Fixture renderer patch copy must not be a link")
endif()
file(WRITE "${_renderer_patch_copy}" "${_renderer_patch_contents}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
  "${GIT_EXECUTABLE}" apply --no-index --unidiff-zero --whitespace=nowarn "${_renderer_patch_copy}"
  WORKING_DIRECTORY "${_fixture_copy}" RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Fixture renderer patch failed: ${error}")
endif()
foreach(entry IN LISTS _fixture_output_hashes)
  string(REPLACE "|" ";" pair "${entry}")
  list(GET pair 0 name)
  list(GET pair 1 expected)
  file(READ "${_fixture_copy}/${name}" contents)
  string(REPLACE "\r\n" "\n" contents "${contents}")
  string(SHA256 actual "${contents}")
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Fixture output hash mismatch: ${name}")
  endif()
endforeach()

# Every unpatched copied file must also remain byte-identical to its input.
set(_fixture_changed_mpv mpvabstractitem.cpp mpvabstractitem.h mpvcontroller.cpp mpvcontroller.h mpvrenderer.cpp)
set(_fixture_changed_player MpvVideoItem.cpp MpvVideoItem.h PlayerMpvEvents.cpp PlayerComponent.cpp PlayerComponent.h)
set(_fixture_changed_core ComponentManager.cpp)
set(_fixture_changed_ui WindowManager.cpp)
set(_fixture_changed_input InputComponent.cpp)
set(_fixture_changed_power PowerComponent.cpp)
foreach(group IN ITEMS mpv player core ui input power)
  foreach(entry IN LISTS _fixture_${group}_inputs)
    string(REPLACE "|" ";" pair "${entry}")
    list(GET pair 0 name)
    list(GET pair 1 expected)
    if(name IN_LIST _fixture_changed_${group})
      continue()
    endif()
    if(group STREQUAL "mpv")
      set(folder mpvqt)
    elseif(group STREQUAL "player")
      set(folder player)
    else()
      set(folder "${group}")
    endif()
    file(READ "${_fixture_copy}/${folder}/${name}" contents)
    string(REPLACE "\r\n" "\n" contents "${contents}")
    string(SHA256 actual "${contents}")
    if(NOT actual STREQUAL expected)
      message(FATAL_ERROR "Fixture unchanged output hash mismatch: ${folder}/${name}")
    endif()
  endforeach()
endforeach()
foreach(entry IN LISTS _fixture_capture_inputs)
  string(REPLACE "|" ";" pair "${entry}")
  list(GET pair 0 name)
  list(GET pair 1 expected)
  file(READ "${_fixture_copy}/mpvqt/${name}" contents)
  string(REPLACE "\r\n" "\n" contents "${contents}")
  string(SHA256 actual "${contents}")
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Fixture capture output hash mismatch: ${name}")
  endif()
endforeach()
file(WRITE "${_fixture_copy}/.fixture-producer-owned" "fixture-producer-copy-v1\n")
