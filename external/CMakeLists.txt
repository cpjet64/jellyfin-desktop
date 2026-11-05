set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMPILER_FLAGS_THIRD_PARTY}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMPILER_FLAGS_THIRD_PARTY}")

if(APPLE)
  add_subdirectory(HIDRemote)
  add_subdirectory(letsmove)
endif(APPLE)

if(USE_STATIC_MPVQT)
  if(MPVQT_EXTERNAL_EVENTS)
    include(PatchedMpvQt)
    if(MPVQT_C2_DIAGNOSTIC_ONLY)
      include(PatchedMpvQtC2)
    endif()
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
  if(MPVQT_C2_DIAGNOSTIC_ONLY)
    target_sources(MpvQt PRIVATE c2/DiagnosticFailureInjection.cpp)
  endif()
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

if(FIXTURE_PRODUCER_COPY_ONLY)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/mpvqt_export.h" "${_fixture_copy}/mpvqt/mpvqt_export.h" COPYONLY)
  set(_mpvqt_src "${_fixture_copy}/mpvqt")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/mpvrenderer-patched-wrapper.cpp.in"
                 "${_fixture_copy}/mpvqt/mpvrenderer-wrapper.cpp" @ONLY)
  file(WRITE "${_fixture_copy}/mpvqt/MpvController" "#include \"mpvcontroller.h\"\n")
  file(WRITE "${_fixture_copy}/mpvqt/MpvAbstractItem" "#include \"mpvabstractitem.h\"\n")
  add_library(FixtureMpvQt STATIC "${_fixture_copy}/mpvqt/mpvabstractitem.cpp"
    "${_fixture_copy}/mpvqt/mpvcontroller.cpp" "${_fixture_copy}/mpvqt/mpvrenderer-wrapper.cpp"
    "${CMAKE_SOURCE_DIR}/tests/HealthyWakeBinding.cpp"
    "${CMAKE_SOURCE_DIR}/tests/fixture_producer/FixtureWakeOwner.cpp")
  target_include_directories(FixtureMpvQt PUBLIC "${_fixture_copy}/mpvqt" "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_SOURCE_DIR}/tests/fixture_producer" "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(FixtureMpvQt PUBLIC Qt6::Quick ${MPV_LIBRARY})
  set_target_properties(FixtureMpvQt PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC ON)
endif()
