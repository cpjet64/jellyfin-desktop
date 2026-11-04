if(MPVQT_C2_DIAGNOSTIC_ONLY)
  set(_c2_input_hashes
    "mpvabstractitem.cpp|8f88c596153886b56f8f73c5534a4744599de284cc832fb96bdd34db45aec97e"
    "mpvabstractitem.h|11e8f2d18a9c1409569d6b711de0083bf13bb5be46a5743a67997fa71700e433"
    "mpvabstractitem_p.h|93e06e780333507d559cad386d07ad9bf734bc9cce24f8ef70dacb1844582fa6"
    "mpvrenderer.cpp|b519e71867813e8bcb73ef4353df1d32700e0ca988575c1e2f4017c1d1d1b798")
  foreach(_entry IN LISTS _c2_input_hashes)
    string(REPLACE "|" ";" _pair "${_entry}")
    list(GET _pair 0 _name)
    list(GET _pair 1 _expected)
    file(READ "${_mpvqt_patched}/${_name}" _contents)
    string(REPLACE "\r\n" "\n" _contents "${_contents}")
    string(SHA256 _actual "${_contents}")
    if(NOT _actual STREQUAL _expected)
      message(FATAL_ERROR "C2 patch input mismatch: ${_name}")
    endif()
  endforeach()
  set(_c2_patch "${CMAKE_SOURCE_DIR}/patches/mpvqt/0002-render-retirement.patch")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_c2_patch}")
  file(READ "${_c2_patch}" _patch_contents)
  string(REPLACE "\r\n" "\n" _patch_contents "${_patch_contents}")
  set(_normalized_patch "${CMAKE_BINARY_DIR}/patched-mpvqt/0002-render-retirement.patch")
  file(WRITE "${_normalized_patch}" "${_patch_contents}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
    "${GIT_EXECUTABLE}" apply --no-index --unidiff-zero --whitespace=nowarn "${_normalized_patch}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/patched-mpvqt" RESULT_VARIABLE _patch_result ERROR_VARIABLE _patch_error)
  if(NOT _patch_result EQUAL 0)
    message(FATAL_ERROR "Unable to apply C2 diagnostic patch: ${_patch_error}")
  endif()
  set(_c2_output_hashes
    "mpvabstractitem.cpp|3fdc338967c9fe5de7262bea0d32daa3841beaaa345252af7871caf80d4d3d49"
    "mpvabstractitem.h|f0d7c9d50019aff85ecf75c680150291d9d32b5dd4a2e04cc6f83886c1f372d3"
    "mpvabstractitem_p.h|8de8db81c9e00c6588262276d77557e6336ace69ffc84041dfdbd2cf4beeae98"
    "mpvrenderer.cpp|6b7c168c361ec6e39e0840588b81fdd28daef56e98fcd2dd1bbf09b88c2f18ca")
  foreach(_entry IN LISTS _c2_output_hashes)
    string(REPLACE "|" ";" _pair "${_entry}")
    list(GET _pair 0 _name)
    list(GET _pair 1 _expected)
    file(READ "${_mpvqt_patched}/${_name}" _contents)
    string(REPLACE "\r\n" "\n" _contents "${_contents}")
    string(SHA256 _actual "${_contents}")
    if(NOT _actual STREQUAL _expected)
      message(FATAL_ERROR "C2 patch output mismatch: ${_name}")
    endif()
  endforeach()
endif()
