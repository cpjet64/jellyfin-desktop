set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMPILER_FLAGS_THIRD_PARTY}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMPILER_FLAGS_THIRD_PARTY}")

if(APPLE)
  add_subdirectory(HIDRemote)
  add_subdirectory(letsmove)
endif(APPLE)

if(USE_STATIC_MPVQT)
  if(MPVQT_EXTERNAL_EVENTS)
    include(PatchedMpvQt)
    set(_mpvqt_src "${_mpvqt_patched}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/mpvqt_export.h" "${_mpvqt_src}/mpvqt_export.h" COPYONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/mpvrenderer-patched-wrapper.cpp.in"
                   "${_mpvqt_src}/mpvrenderer-wrapper.cpp" @ONLY)
    set(_mpvqt_wrapper "${_mpvqt_src}/mpvrenderer-wrapper.cpp")
  else()
    set(_mpvqt_src "${CMAKE_CURRENT_SOURCE_DIR}/mpvqt/src")
    set(_mpvqt_wrapper "${CMAKE_CURRENT_SOURCE_DIR}/mpvrenderer-wrapper.cpp")
  endif()
  # Build MpvQt directly from sources
  add_library(MpvQt STATIC
    ${_mpvqt_src}/mpvabstractitem.cpp
    ${_mpvqt_src}/mpvcontroller.cpp
    ${_mpvqt_wrapper}
  )
  target_include_directories(MpvQt PUBLIC
    ${_mpvqt_src}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/MpvQt
  )
  target_link_libraries(MpvQt PUBLIC
    Qt6::Quick
    ${MPV_LIBRARY}
  )
  set_target_properties(MpvQt PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
  )
  # Create Qt-style forwarding headers
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/MpvQt/MpvAbstractItem "#include \"mpvabstractitem.h\"\n")
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/MpvQt/MpvController "#include \"mpvcontroller.h\"\n")
endif()
