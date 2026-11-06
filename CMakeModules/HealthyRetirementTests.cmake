# Headless tests of the diagnostic retirement protocol and its Qt adapter.
find_package(Threads REQUIRED)
add_executable(test_healthy_module_launch "${CMAKE_SOURCE_DIR}/tests/test_healthy_module_launch.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_channel.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_gate.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_identity.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_launch_test_stubs.cpp")
target_include_directories(test_healthy_module_launch PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_features(test_healthy_module_launch PRIVATE cxx_std_17)
target_compile_definitions(test_healthy_module_launch PRIVATE HEALTHY_MODULE_TESTING)
if(WIN32)
  target_sources(test_healthy_module_launch PRIVATE "${CMAKE_SOURCE_DIR}/tests/fixture_restart/WindowsFrameReader.cpp"
    "${CMAKE_SOURCE_DIR}/tests/fixture_restart/WindowsRestartIo.cpp")
  target_link_libraries(test_healthy_module_launch PRIVATE Qt6::Core onecore)
endif()
add_test(NAME test_healthy_module_launch COMMAND test_healthy_module_launch)
set_tests_properties(test_healthy_module_launch PROPERTIES TIMEOUT 60)

add_executable(test_healthy_paused_hold "${CMAKE_SOURCE_DIR}/tests/test_healthy_paused_hold.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/paused_hold.cpp"
  "${CMAKE_SOURCE_DIR}/tests/fixture_restart/frame_acceptance.cpp")
target_include_directories(test_healthy_paused_hold PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_include_directories(test_healthy_paused_hold PRIVATE "${CMAKE_SOURCE_DIR}/tests/fixture_restart")
target_compile_features(test_healthy_paused_hold PRIVATE cxx_std_17)
target_compile_definitions(test_healthy_paused_hold PRIVATE HEALTHY_RETIREMENT_TESTING)
add_test(NAME test_healthy_paused_hold COMMAND test_healthy_paused_hold)
set_tests_properties(test_healthy_paused_hold PROPERTIES TIMEOUT 60)

add_executable(test_healthy_resume_progress "${CMAKE_SOURCE_DIR}/tests/test_healthy_resume_progress.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/resume_progress.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/paused_hold.cpp"
  "${CMAKE_SOURCE_DIR}/tests/fixture_restart/frame_acceptance.cpp")
target_include_directories(test_healthy_resume_progress PRIVATE "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/tests/fixture_restart")
target_compile_features(test_healthy_resume_progress PRIVATE cxx_std_17)
target_compile_definitions(test_healthy_resume_progress PRIVATE HEALTHY_RETIREMENT_TESTING)
add_test(NAME test_healthy_resume_progress COMMAND test_healthy_resume_progress)
set_tests_properties(test_healthy_resume_progress PROPERTIES TIMEOUT 60)

add_executable(test_restart_closed_wire "${CMAKE_SOURCE_DIR}/tests/test_restart_closed_wire.cpp"
  "${CMAKE_SOURCE_DIR}/tests/fixture_restart/RestartWire.cpp"
  "${CMAKE_SOURCE_DIR}/tests/fixture_restart/FixtureEvidence.cpp"
  "${CMAKE_SOURCE_DIR}/src/player/PlaybackCheckpoint.cpp")
target_include_directories(test_restart_closed_wire PRIVATE "${CMAKE_SOURCE_DIR}/tests" "${CMAKE_SOURCE_DIR}/src")
target_compile_features(test_restart_closed_wire PRIVATE cxx_std_17)
target_link_libraries(test_restart_closed_wire PRIVATE Qt6::Core)
add_test(NAME test_restart_closed_wire COMMAND test_restart_closed_wire)
set_tests_properties(test_restart_closed_wire PROPERTIES TIMEOUT 60)

foreach(name IN ITEMS lifetime races)
  add_executable(test_healthy_retirement_${name}
    "${CMAKE_SOURCE_DIR}/tests/test_healthy_retirement_${name}.cpp"
    "${CMAKE_SOURCE_DIR}/tests/healthy_restart/lifetime.cpp")
  target_include_directories(test_healthy_retirement_${name} PRIVATE "${CMAKE_SOURCE_DIR}/tests")
  target_compile_features(test_healthy_retirement_${name} PRIVATE cxx_std_17)
  target_link_libraries(test_healthy_retirement_${name} PRIVATE Threads::Threads)
  add_test(NAME test_healthy_retirement_${name} COMMAND test_healthy_retirement_${name})
  set_tests_properties(test_healthy_retirement_${name} PROPERTIES TIMEOUT 60)
endforeach()

add_executable(test_healthy_retirement_qt
  "${CMAKE_SOURCE_DIR}/tests/test_healthy_retirement_qt.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/lifetime.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/lifetime_qt.cpp")
target_include_directories(test_healthy_retirement_qt PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_features(test_healthy_retirement_qt PRIVATE cxx_std_17)
target_link_libraries(test_healthy_retirement_qt PRIVATE Threads::Threads Qt6::Core Qt6::Gui)
add_test(NAME test_healthy_retirement_qt COMMAND test_healthy_retirement_qt)
set_tests_properties(test_healthy_retirement_qt PROPERTIES TIMEOUT 60)

add_executable(test_healthy_retirement_owner
  "${CMAKE_SOURCE_DIR}/tests/test_healthy_retirement_owner.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/owner.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/lifetime.cpp")
target_include_directories(test_healthy_retirement_owner PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_features(test_healthy_retirement_owner PRIVATE cxx_std_17)
target_compile_definitions(test_healthy_retirement_owner PRIVATE HEALTHY_RETIREMENT_TESTING)
target_link_libraries(test_healthy_retirement_owner PRIVATE Threads::Threads)
add_test(NAME test_healthy_retirement_owner COMMAND test_healthy_retirement_owner)
set_tests_properties(test_healthy_retirement_owner PROPERTIES TIMEOUT 60)

add_executable(test_healthy_retirement_owner_context
  "${CMAKE_SOURCE_DIR}/tests/test_healthy_retirement_owner_context.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/lifetime.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/lifetime_qt.cpp")
target_include_directories(test_healthy_retirement_owner_context PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_features(test_healthy_retirement_owner_context PRIVATE cxx_std_17)
target_link_libraries(test_healthy_retirement_owner_context PRIVATE Threads::Threads Qt6::Core Qt6::Gui)
add_test(NAME test_healthy_retirement_owner_context COMMAND test_healthy_retirement_owner_context)
set_tests_properties(test_healthy_retirement_owner_context PROPERTIES TIMEOUT 60)

add_executable(test_healthy_module_identity
  "${CMAKE_SOURCE_DIR}/tests/test_healthy_module_identity.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_identity.cpp")
target_include_directories(test_healthy_module_identity PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_features(test_healthy_module_identity PRIVATE cxx_std_17)
add_test(NAME test_healthy_module_identity COMMAND test_healthy_module_identity)
set_tests_properties(test_healthy_module_identity PROPERTIES TIMEOUT 60)

if(NOT WIN32)
  add_library(healthy_module_identity_adapter_compile OBJECT
    "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_identity_win.cpp")
  target_compile_features(healthy_module_identity_adapter_compile PRIVATE cxx_std_17)
endif()

add_executable(test_healthy_graph_retirement "${CMAKE_SOURCE_DIR}/tests/test_healthy_graph_retirement.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/graph_retirement.cpp" "${CMAKE_SOURCE_DIR}/tests/healthy_restart/owner.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/lifetime.cpp" "${CMAKE_SOURCE_DIR}/tests/healthy_restart/lifetime_module.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/closure.cpp")
target_include_directories(test_healthy_graph_retirement PRIVATE "${CMAKE_SOURCE_DIR}/tests/healthy_restart")
target_compile_features(test_healthy_graph_retirement PRIVATE cxx_std_17)
target_compile_definitions(test_healthy_graph_retirement PRIVATE HEALTHY_RETIREMENT_TESTING)
target_link_libraries(test_healthy_graph_retirement PRIVATE Threads::Threads Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml)
add_test(NAME test_healthy_graph_retirement COMMAND test_healthy_graph_retirement)
set_tests_properties(test_healthy_graph_retirement PROPERTIES TIMEOUT 60)

add_executable(test_healthy_graph_retirement_guard "${CMAKE_SOURCE_DIR}/tests/test_healthy_graph_retirement_guard.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/lifetime.cpp" "${CMAKE_SOURCE_DIR}/tests/healthy_restart/lifetime_module.cpp")
target_include_directories(test_healthy_graph_retirement_guard PRIVATE "${CMAKE_SOURCE_DIR}/tests/healthy_restart")
target_compile_features(test_healthy_graph_retirement_guard PRIVATE cxx_std_17)
target_compile_definitions(test_healthy_graph_retirement_guard PRIVATE HEALTHY_RETIREMENT_TESTING)
target_link_libraries(test_healthy_graph_retirement_guard PRIVATE Threads::Threads)
add_test(NAME test_healthy_graph_retirement_guard COMMAND test_healthy_graph_retirement_guard)
set_tests_properties(test_healthy_graph_retirement_guard PROPERTIES TIMEOUT 60)

add_executable(test_healthy_graph_retirement_access "${CMAKE_SOURCE_DIR}/tests/test_healthy_graph_retirement_access.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/graph_access.cpp")
target_include_directories(test_healthy_graph_retirement_access PRIVATE "${CMAKE_SOURCE_DIR}/tests/healthy_restart")
target_compile_features(test_healthy_graph_retirement_access PRIVATE cxx_std_17)
target_compile_definitions(test_healthy_graph_retirement_access PRIVATE HEALTHY_RETIREMENT_TESTING)
target_link_libraries(test_healthy_graph_retirement_access PRIVATE Threads::Threads Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml)
add_test(NAME test_healthy_graph_retirement_access COMMAND test_healthy_graph_retirement_access)
set_tests_properties(test_healthy_graph_retirement_access PROPERTIES TIMEOUT 60)

add_executable(test_healthy_identity_commitment "${CMAKE_SOURCE_DIR}/tests/test_healthy_identity_commitment.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_gate.cpp" "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_identity.cpp")
target_include_directories(test_healthy_identity_commitment PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_features(test_healthy_identity_commitment PRIVATE cxx_std_17)
add_test(NAME test_healthy_identity_commitment COMMAND test_healthy_identity_commitment)
set_tests_properties(test_healthy_identity_commitment PROPERTIES TIMEOUT 60)

if(WIN32)
  foreach(name IN ITEMS child_session child_session_source)
    add_executable(test_healthy_${name} "${CMAKE_SOURCE_DIR}/tests/test_healthy_${name}.cpp"
      "${CMAKE_SOURCE_DIR}/tests/healthy_restart/child_session.cpp"
      "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_endpoints.cpp"
      "${CMAKE_SOURCE_DIR}/tests/fixture_restart/WindowsRestartIo.cpp"
      "${CMAKE_SOURCE_DIR}/tests/fixture_restart/RestartWire.cpp"
      "${CMAKE_SOURCE_DIR}/tests/fixture_restart/FixtureEvidence.cpp"
      "${CMAKE_SOURCE_DIR}/src/player/PlaybackCheckpoint.cpp")
    target_include_directories(test_healthy_${name} PRIVATE "${CMAKE_SOURCE_DIR}/tests" "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(test_healthy_${name} PRIVATE cxx_std_17)
    target_compile_definitions(test_healthy_${name} PRIVATE HEALTHY_CHILD_SESSION_TESTING
      HEALTHY_CHILD_SESSION_HEADLESS HEALTHY_ENDPOINT_TESTING)
    target_link_libraries(test_healthy_${name} PRIVATE Qt6::Core Qt6::Gui Qt6::Quick onecore)
    add_test(NAME test_healthy_${name} COMMAND test_healthy_${name})
    set_tests_properties(test_healthy_${name} PROPERTIES TIMEOUT 60)
  endforeach()
  add_executable(test_healthy_module_gate_endpoints "${CMAKE_SOURCE_DIR}/tests/test_healthy_module_gate_endpoints.cpp"
    "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_endpoints.cpp" "${CMAKE_SOURCE_DIR}/tests/fixture_restart/WindowsRestartIo.cpp")
  target_include_directories(test_healthy_module_gate_endpoints PRIVATE "${CMAKE_SOURCE_DIR}/tests")
  target_compile_features(test_healthy_module_gate_endpoints PRIVATE cxx_std_17)
  target_compile_definitions(test_healthy_module_gate_endpoints PRIVATE HEALTHY_ENDPOINT_TESTING)
  target_link_libraries(test_healthy_module_gate_endpoints PRIVATE onecore)
  add_test(NAME test_healthy_module_gate_endpoints COMMAND test_healthy_module_gate_endpoints)
  set_tests_properties(test_healthy_module_gate_endpoints PROPERTIES TIMEOUT 60)
endif()

add_executable(test_healthy_closure "${CMAKE_SOURCE_DIR}/tests/test_healthy_closure.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/closure.cpp" "${CMAKE_SOURCE_DIR}/tests/healthy_restart/lifetime.cpp")
target_include_directories(test_healthy_closure PRIVATE "${CMAKE_SOURCE_DIR}/tests/healthy_restart")
target_compile_definitions(test_healthy_closure PRIVATE HEALTHY_RETIREMENT_TESTING)
target_compile_features(test_healthy_closure PRIVATE cxx_std_17)
target_link_libraries(test_healthy_closure PRIVATE Threads::Threads Qt6::Core)
add_test(NAME test_healthy_closure COMMAND test_healthy_closure)
set_tests_properties(test_healthy_closure PROPERTIES TIMEOUT 60)

add_executable(test_healthy_closure_wake "${CMAKE_SOURCE_DIR}/tests/test_healthy_closure_wake.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/closure_wake.cpp")
target_include_directories(test_healthy_closure_wake PRIVATE "${CMAKE_SOURCE_DIR}/tests/healthy_restart")
target_compile_definitions(test_healthy_closure_wake PRIVATE HEALTHY_RETIREMENT_TESTING)
target_compile_features(test_healthy_closure_wake PRIVATE cxx_std_17)
target_link_libraries(test_healthy_closure_wake PRIVATE Threads::Threads)
add_test(NAME test_healthy_closure_wake COMMAND test_healthy_closure_wake)
set_tests_properties(test_healthy_closure_wake PROPERTIES TIMEOUT 60)

add_executable(test_healthy_async_stop "${CMAKE_SOURCE_DIR}/tests/healthy_restart/test_async_stop.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/async_stop.cpp")
target_compile_features(test_healthy_async_stop PRIVATE cxx_std_17)
add_test(NAME test_healthy_async_stop COMMAND test_healthy_async_stop)
set_tests_properties(test_healthy_async_stop PROPERTIES TIMEOUT 60)

add_executable(test_healthy_module_gate_wire "${CMAKE_SOURCE_DIR}/tests/test_healthy_module_gate_wire.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_gate.cpp" "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_identity.cpp")
target_include_directories(test_healthy_module_gate_wire PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_features(test_healthy_module_gate_wire PRIVATE cxx_std_17)
add_test(NAME test_healthy_module_gate_wire COMMAND test_healthy_module_gate_wire)
set_tests_properties(test_healthy_module_gate_wire PROPERTIES TIMEOUT 60)

add_executable(test_healthy_module_gate_import "${CMAKE_SOURCE_DIR}/tests/test_healthy_module_gate_import.cpp"
  "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_import_prepare.cpp")
target_include_directories(test_healthy_module_gate_import PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(test_healthy_module_gate_import PRIVATE HEALTHY_MODULE_TESTING)
target_compile_features(test_healthy_module_gate_import PRIVATE cxx_std_17)
if(WIN32)
  target_link_libraries(test_healthy_module_gate_import PRIVATE delayimp)
endif()
add_test(NAME test_healthy_module_gate_import COMMAND test_healthy_module_gate_import)
set_tests_properties(test_healthy_module_gate_import PROPERTIES TIMEOUT 60)

if(WIN32)
  add_executable(test_healthy_module_gate_factory
    "${CMAKE_SOURCE_DIR}/tests/test_healthy_module_gate_factory.cpp"
    "${CMAKE_SOURCE_DIR}/tests/fixture_restart/WindowsRestartHealthy.cpp"
    "${CMAKE_SOURCE_DIR}/tests/fixture_restart/WindowsRestartProcess.cpp"
    "${CMAKE_SOURCE_DIR}/tests/fixture_restart/WindowsRestartChannel.cpp"
    "${CMAKE_SOURCE_DIR}/tests/fixture_restart/WindowsRestartIo.cpp"
    "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_identity_win.cpp"
    "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_identity.cpp"
    "${CMAKE_SOURCE_DIR}/tests/healthy_restart/module_gate.cpp")
  target_include_directories(test_healthy_module_gate_factory PRIVATE "${CMAKE_SOURCE_DIR}/tests")
  target_compile_features(test_healthy_module_gate_factory PRIVATE cxx_std_17)
  target_compile_definitions(test_healthy_module_gate_factory PRIVATE HEALTHY_MODULE_TESTING)
  target_link_libraries(test_healthy_module_gate_factory PRIVATE Threads::Threads Qt6::Core advapi32 bcrypt)
  add_test(NAME test_healthy_module_gate_factory COMMAND test_healthy_module_gate_factory)
  set_tests_properties(test_healthy_module_gate_factory PROPERTIES TIMEOUT 60)
endif()
