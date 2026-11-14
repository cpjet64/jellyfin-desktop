# Apply only after the pinned C1 output has been verified by PatchedMpvQt.
set(_recovery_inputs
  "mpvabstractitem.cpp|8f88c596153886b56f8f73c5534a4744599de284cc832fb96bdd34db45aec97e"
  "mpvabstractitem.h|11e8f2d18a9c1409569d6b711de0083bf13bb5be46a5743a67997fa71700e433"
  "mpvabstractitem_p.h|93e06e780333507d559cad386d07ad9bf734bc9cce24f8ef70dacb1844582fa6"
  "mpvrenderer.cpp|b519e71867813e8bcb73ef4353df1d32700e0ca988575c1e2f4017c1d1d1b798"
  "mpvrenderer.h|a35f52d0d4641db2c4bb03d0ced7ba40864f6e6b471b2cb42cc3e7068dda919f")
set(_recovery_outputs
  "mpvabstractitem.cpp|5a03f67254aa2d663ac7c7ec5893ae4868c8e94eeffdd11d620d1ee813aeef32"
  "mpvabstractitem.h|cb6a8c54f4ba7ea96245a47ee529ed6801bc08eaf0eebf274869dc395b149f07"
  "mpvabstractitem_p.h|fb095701688d77140f266eb3fe0f6878e33b67d7de63b153d5208c7cd7793d62"
  "mpvrenderer.cpp|966cea4fd4f24d2ae3ed4595a3fd35476f94f5da54017fcd36dfbeb96692ec16"
  "mpvrenderer.h|5a5db70004a243f6a5e608462aacafb2f7174048b2c75e304785e3a8cc0ff6a5")

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
