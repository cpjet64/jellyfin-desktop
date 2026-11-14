# Apply only after the pinned C1 output has been verified by PatchedMpvQt.
set(_recovery_inputs
  "mpvabstractitem.cpp|8f88c596153886b56f8f73c5534a4744599de284cc832fb96bdd34db45aec97e"
  "mpvabstractitem.h|11e8f2d18a9c1409569d6b711de0083bf13bb5be46a5743a67997fa71700e433"
  "mpvabstractitem_p.h|93e06e780333507d559cad386d07ad9bf734bc9cce24f8ef70dacb1844582fa6"
  "mpvrenderer.cpp|b519e71867813e8bcb73ef4353df1d32700e0ca988575c1e2f4017c1d1d1b798"
  "mpvrenderer.h|a35f52d0d4641db2c4bb03d0ced7ba40864f6e6b471b2cb42cc3e7068dda919f")
set(_recovery_outputs
  "mpvabstractitem.cpp|4ca0194875ae970928540c33fd1c79dce7c951568a32833d0f11ee8d597d5b31"
  "mpvabstractitem.h|0f1141465176ef506dc34923917288cc238143f0fc2988b2fcae5429f1ea55f3"
  "mpvabstractitem_p.h|c4efdd58681eea6fe7760f23f45c67b0a69395d6470361a920aeb9b6feedb2cb"
  "mpvrenderer.cpp|3d0b18b4b7718e0bafab20279ba850537697408eb33ce77d57846b281e60aff2"
  "mpvrenderer.h|77bd03874e8e420fc09eba17a45d1b2359249227f42bee258fbaa0ea2d807fbb")

function(_verify_recovery_sources entries)
  foreach(_entry IN LISTS ${entries})
    string(REPLACE "|" ";" _pair "${_entry}")
    list(GET _pair 0 _name)
    list(GET _pair 1 _expected)
    file(READ "${_mpvqt_patched}/${_name}" _contents)
    string(REPLACE "\r\n" "\n" _contents "${_contents}")
    string(SHA256 _actual "${_contents}")
    if(NOT _actual STREQUAL _expected)
      message(FATAL_ERROR "Recovery MpvQt source mismatch: ${_name}")
    endif()
  endforeach()
endfunction()

_verify_recovery_sources(_recovery_inputs)
foreach(_patch_name IN ITEMS 0003-recovery-owner.patch 0003-renderer-recovery.patch)
  set(_recovery_patch "${CMAKE_SOURCE_DIR}/patches/mpvqt/${_patch_name}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_recovery_patch}")
  file(READ "${_recovery_patch}" _patch_contents)
  string(REPLACE "\r\n" "\n" _patch_contents "${_patch_contents}")
  set(_normalized_patch "${CMAKE_BINARY_DIR}/patched-mpvqt/${_patch_name}")
  file(WRITE "${_normalized_patch}" "${_patch_contents}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
    "${GIT_EXECUTABLE}" apply --no-index --unidiff-zero --whitespace=nowarn "${_normalized_patch}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/patched-mpvqt" RESULT_VARIABLE _patch_result ERROR_VARIABLE _patch_error)
  if(NOT _patch_result EQUAL 0)
    message(FATAL_ERROR "Unable to apply recovery MpvQt patch ${_patch_name}: ${_patch_error}")
  endif()
endforeach()
_verify_recovery_sources(_recovery_outputs)

set(_controller_inputs
  "mpvcontroller.cpp|9dd28536d4e3241f3983281b1609669fbf10335c966315f4028352e130d83ec0"
  "mpvcontroller.h|42cf9e3126308bbcce4ba62b8d6f3a4cbba734806ed422c0e160f8cb5ad009e0")
set(_controller_outputs
  "mpvcontroller.cpp|59035ce9531d069c284481bf93e8a976e38bf5c36e8b62b1b96b9597152ec02f"
  "mpvcontroller.h|d0b9c60aa0bdd8f1bb6667006ab7e13bace5287e9662d75d56b550e5cacd15bd")
_verify_recovery_sources(_controller_inputs)
set(_controller_patch "${CMAKE_SOURCE_DIR}/patches/mpvqt/0004-controller-native-lease.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_controller_patch}")
file(READ "${_controller_patch}" _patch_contents)
string(REPLACE "\r\n" "\n" _patch_contents "${_patch_contents}")
set(_normalized_patch "${CMAKE_BINARY_DIR}/patched-mpvqt/0004-controller-native-lease.patch")
file(WRITE "${_normalized_patch}" "${_patch_contents}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
  "${GIT_EXECUTABLE}" apply --no-index --unidiff-zero --whitespace=nowarn "${_normalized_patch}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/patched-mpvqt" RESULT_VARIABLE _patch_result ERROR_VARIABLE _patch_error)
if(NOT _patch_result EQUAL 0)
  message(FATAL_ERROR "Unable to apply controller lease patch: ${_patch_error}")
endif()
_verify_recovery_sources(_controller_outputs)
