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
  "PlayerComponent.cpp|6a4cf1edfa6831e1d5608918ece36afadce587fe7804beda88c64b9544cc9092"
  "PlayerComponent.h|38fdc3fe0a8edb56b3b5aff8a1142552c0dfa01266701bd09e33bdc269618106"
  "PlayerComponentCommands.cpp|cad7f30600d2df6eea4b6f910a813c553f61bde70fc303ba103eaa42dee9deb9"
  "PlayerIntentObservation.h|dc2bf8491cb58d2b30156245d18c8590a3ebebbfe3932c9bb1df97c1bdaebe31"
  "PlayerMpvEvents.cpp|53ad9afccbeff2934503156f15b612a65795bc5e73efaf214d99da4a90036557"
  "PlayerReleasedState.cpp|273d7001db537e184dc4527b8571e7944973c68fbf8cfb2546aab87defca7f86"
  "QtHelper.h|87fbb5a65a740998e9da037f1527906b214d90f758586538d1395237ac4a388a"
  "WindowsMpvLoader.cpp|8a1070a83908a1510896905bc0d156a518de09d637324eba997a97e6952f9897")
set(_fixture_test_inputs
  "test_webclient_containment.cpp|75de059742269bc205841cc2026114f5eb0cf6a478caade681a3a8ff07f8486b")
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
  "player/MpvVideoItem.cpp|df2d8f3429067ef94cf4e737bad91555ff3d47545c6a03502a6763267895c2f8"
  "player/MpvVideoItem.h|697c7c2d34cdf99c266dcab77623a9be5c35ab966cb555739932159e8bed13cf"
  "player/PlayerMpvEvents.cpp|44cd5d38f80527b8577c796c0cbd413e0a031369f53e8afcb6f897580d4563bb"
  "player/PlayerComponent.cpp|fadf5c172759ffd394ba2870ef7e3408bd1b04596c0b611279f5ad2a60f55377"
  "player/PlayerComponent.h|754d26066d120c3fd8f493fa87698d38ff8f380dd333df7b14706f169d2a8b4e"
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
foreach(group IN ITEMS core ui input power)
  _fixture_copy_inputs("${CMAKE_SOURCE_DIR}/src/${group}" "${_fixture_copy}/${group}" _fixture_${group}_inputs)
endforeach()
set(_fixture_patch "${CMAKE_SOURCE_DIR}/patches/fixture-producer/0001-producer-binding.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_fixture_patch}")
file(READ "${_fixture_patch}" _fixture_patch_contents)
string(REPLACE "\r\n" "\n" _fixture_patch_contents "${_fixture_patch_contents}")
string(SHA256 _fixture_patch_hash "${_fixture_patch_contents}")
if(NOT _fixture_patch_hash STREQUAL "beab0fad9cecb8e5d98c8a85ad10485d4513731b2f43031e92498c7ac2bd2bec")
  message(FATAL_ERROR "Fixture patch hash mismatch")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
  "${GIT_EXECUTABLE}" apply --no-index --unidiff-zero --whitespace=nowarn "${_fixture_patch}"
  WORKING_DIRECTORY "${_fixture_copy}" RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Fixture source patch failed: ${error}")
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
set(_fixture_changed_mpv mpvabstractitem.cpp mpvabstractitem.h mpvcontroller.cpp mpvcontroller.h)
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
file(WRITE "${_fixture_copy}/.fixture-producer-owned" "fixture-producer-copy-v1\n")
