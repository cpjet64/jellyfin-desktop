# Prepare the reviewed MpvQt 2ebbd38 sources without modifying the submodule.
find_package(Git REQUIRED)
set(_mpvqt_source "${CMAKE_SOURCE_DIR}/external/mpvqt/src")
set(_mpvqt_patched "${CMAKE_BINARY_DIR}/patched-mpvqt/src")
set(_mpvqt_files
  "mpvabstractitem.cpp|1cd2b083e531da9d67465bda282f1ea085fb01281bfa26a6a4e2451809ff2de0"
  "mpvabstractitem.h|d166944d4ca34a9f9499b0ff00c89c0216f10760022293833883741b7e156a27"
  "mpvabstractitem_p.h|93e06e780333507d559cad386d07ad9bf734bc9cce24f8ef70dacb1844582fa6"
  "mpvcontroller.cpp|0c2d0d9c091cee55c497f63fc13b00eead20767eca2f75575b601d471e0fe999"
  "mpvcontroller.h|eede2d838a167de76fe801804dcb3c6cc9094b02c437a9b551c6d0802bb474a8"
  "mpvcontroller_p.h|0ce2f019dec773cca79fc2bcd3f45761793f5150a2e1290963ace2d85ec3daed"
  "mpvrenderer.cpp|b519e71867813e8bcb73ef4353df1d32700e0ca988575c1e2f4017c1d1d1b798"
  "mpvrenderer.h|a35f52d0d4641db2c4bb03d0ced7ba40864f6e6b471b2cb42cc3e7068dda919f")
file(MAKE_DIRECTORY "${_mpvqt_patched}")
foreach(_entry IN LISTS _mpvqt_files)
  string(REPLACE "|" ";" _pair "${_entry}")
  list(GET _pair 0 _name)
  list(GET _pair 1 _expected)
  set(_path "${_mpvqt_source}/${_name}")
  if(NOT EXISTS "${_path}")
    message(FATAL_ERROR "Pinned MpvQt source missing: ${_path}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_path}")
  file(READ "${_path}" _contents)
  string(REPLACE "\r\n" "\n" _contents "${_contents}")
  string(SHA256 _actual "${_contents}")
  if(NOT _actual STREQUAL _expected)
    message(FATAL_ERROR "Pinned MpvQt 2ebbd38 source mismatch: ${_path}")
  endif()
  file(WRITE "${_mpvqt_patched}/${_name}" "${_contents}")
endforeach()
set(_mpvqt_patch "${CMAKE_SOURCE_DIR}/patches/mpvqt/0001-event-ownership.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_mpvqt_patch}")
file(READ "${_mpvqt_patch}" _patch_contents)
string(REPLACE "\r\n" "\n" _patch_contents "${_patch_contents}")
set(_normalized_patch "${CMAKE_BINARY_DIR}/patched-mpvqt/0001-event-ownership.patch")
file(WRITE "${_normalized_patch}" "${_patch_contents}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
  "${GIT_EXECUTABLE}" apply --no-index --unidiff-zero --whitespace=nowarn "${_normalized_patch}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/patched-mpvqt" RESULT_VARIABLE _patch_result ERROR_VARIABLE _patch_error)
if(NOT _patch_result EQUAL 0)
  message(FATAL_ERROR "Unable to apply bundled MpvQt patch: ${_patch_error}")
endif()
set(_mpvqt_patched_hashes
  "mpvabstractitem.cpp|8f88c596153886b56f8f73c5534a4744599de284cc832fb96bdd34db45aec97e"
  "mpvabstractitem.h|11e8f2d18a9c1409569d6b711de0083bf13bb5be46a5743a67997fa71700e433"
  "mpvcontroller.cpp|9dd28536d4e3241f3983281b1609669fbf10335c966315f4028352e130d83ec0"
  "mpvcontroller.h|42cf9e3126308bbcce4ba62b8d6f3a4cbba734806ed422c0e160f8cb5ad009e0")
foreach(_entry IN LISTS _mpvqt_patched_hashes)
  string(REPLACE "|" ";" _pair "${_entry}")
  list(GET _pair 0 _name)
  list(GET _pair 1 _expected)
  file(READ "${_mpvqt_patched}/${_name}" _contents)
  string(REPLACE "\r\n" "\n" _contents "${_contents}")
  string(SHA256 _actual "${_contents}")
  if(NOT _actual STREQUAL _expected)
    message(FATAL_ERROR "Patched MpvQt output mismatch: ${_name}")
  endif()
endforeach()
