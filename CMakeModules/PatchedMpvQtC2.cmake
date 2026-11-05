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
    "mpvabstractitem.cpp|8eb1af12b836ac6f4659528740f0deae259e3b9709160106b8a2481f94c8e031"
    "mpvabstractitem.h|64f3e7f89e0c9084c7f942dc77cd4844e2a33c340a676853bf990004a47ceae2"
    "mpvabstractitem_p.h|7aafa898580ec1cc92b53fb1e2dcabf336cc0f2e6b13c3c6164f67638ce21c81"
    "mpvrenderer.cpp|74526659cfce317a7e60d86611b65e4371b1b797e2eb1db74807d9b24adad107")
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
