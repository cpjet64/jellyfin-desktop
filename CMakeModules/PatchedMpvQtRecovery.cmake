# Apply only after the pinned C1 output has been verified by PatchedMpvQt.
set(_recovery_inputs
  "mpvabstractitem.cpp|8f88c596153886b56f8f73c5534a4744599de284cc832fb96bdd34db45aec97e"
  "mpvabstractitem.h|11e8f2d18a9c1409569d6b711de0083bf13bb5be46a5743a67997fa71700e433"
  "mpvabstractitem_p.h|93e06e780333507d559cad386d07ad9bf734bc9cce24f8ef70dacb1844582fa6"
  "mpvrenderer.cpp|b519e71867813e8bcb73ef4353df1d32700e0ca988575c1e2f4017c1d1d1b798"
  "mpvrenderer.h|a35f52d0d4641db2c4bb03d0ced7ba40864f6e6b471b2cb42cc3e7068dda919f")
set(_recovery_outputs
  "mpvabstractitem.cpp|7a9525d9d2167e3cabdce29591ec8a0a479dadeb7b7945d16124b4439fe19f3e"
  "mpvabstractitem.h|220e7dc1f8cb867fbbc5e519cafc281b962fdd0ec0f32df7245f4c0630f50af5"
  "mpvabstractitem_p.h|fb095701688d77140f266eb3fe0f6878e33b67d7de63b153d5208c7cd7793d62"
  "mpvrenderer.cpp|f6cbd74925ab1c36b0dc6723f829e9a4ffe82adca55a2046b20b089e3836a6f5"
  "mpvrenderer.h|62f29c7d6ff0ed11f931d1ea6e8c62d419c2f7c3999713220a1844380c0e5525")

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
set(_recovery_patch "${CMAKE_SOURCE_DIR}/patches/mpvqt/0003-recovery-owner.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_recovery_patch}")
file(READ "${_recovery_patch}" _patch_contents)
string(REPLACE "\r\n" "\n" _patch_contents "${_patch_contents}")
set(_normalized_patch "${CMAKE_BINARY_DIR}/patched-mpvqt/0003-recovery-owner.patch")
file(WRITE "${_normalized_patch}" "${_patch_contents}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
  "${GIT_EXECUTABLE}" apply --no-index --unidiff-zero --whitespace=nowarn "${_normalized_patch}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/patched-mpvqt" RESULT_VARIABLE _patch_result ERROR_VARIABLE _patch_error)
if(NOT _patch_result EQUAL 0)
  message(FATAL_ERROR "Unable to apply recovery MpvQt patch: ${_patch_error}")
endif()
_verify_recovery_sources(_recovery_outputs)

set(_controller_inputs
  "mpvcontroller.cpp|9dd28536d4e3241f3983281b1609669fbf10335c966315f4028352e130d83ec0"
  "mpvcontroller.h|42cf9e3126308bbcce4ba62b8d6f3a4cbba734806ed422c0e160f8cb5ad009e0")
set(_controller_outputs
  "mpvcontroller.cpp|b9de4c9d7aaa6bdda85786e40ab0a6a4a085933e5e002bbd225963262322234e"
  "mpvcontroller.h|5c254bdc8f3623448951099af3ff9a4aa789c3b7ea55cf3b11d1c7a5ac813569")
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
