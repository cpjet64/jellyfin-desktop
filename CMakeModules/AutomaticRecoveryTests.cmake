add_executable(test_automatic_playback_recovery
  "${CMAKE_SOURCE_DIR}/tests/test_automatic_playback_recovery.cpp"
  "${CMAKE_SOURCE_DIR}/src/player/AutomaticRecoveryPolicy.cpp")
target_include_directories(test_automatic_playback_recovery PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(test_automatic_playback_recovery PRIVATE cxx_std_17)
add_test(NAME test_automatic_playback_recovery COMMAND test_automatic_playback_recovery)
set_tests_properties(test_automatic_playback_recovery PROPERTIES TIMEOUT 60)

add_executable(test_automatic_recovery_load
  "${CMAKE_SOURCE_DIR}/tests/test_automatic_recovery_load.cpp"
  "${CMAKE_SOURCE_DIR}/src/player/AutomaticRecoveryLoad.cpp")
target_include_directories(test_automatic_recovery_load PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(test_automatic_recovery_load PRIVATE cxx_std_17)
add_test(NAME test_automatic_recovery_load COMMAND test_automatic_recovery_load)
set_tests_properties(test_automatic_recovery_load PROPERTIES TIMEOUT 60)

add_executable(test_automatic_recovery_native
  "${CMAKE_SOURCE_DIR}/tests/test_automatic_recovery_native.cpp"
  "${CMAKE_SOURCE_DIR}/src/player/AutomaticRecoveryNative.cpp"
  "${CMAKE_SOURCE_DIR}/src/player/AutomaticRecoverySession.cpp")
target_include_directories(test_automatic_recovery_native PRIVATE "${CMAKE_SOURCE_DIR}/src" "${MPV_INCLUDE_DIR}")
target_compile_features(test_automatic_recovery_native PRIVATE cxx_std_17)
target_link_libraries(test_automatic_recovery_native PRIVATE Qt6::Core)
add_test(NAME test_automatic_recovery_native COMMAND test_automatic_recovery_native)
set_tests_properties(test_automatic_recovery_native PROPERTIES TIMEOUT 60)

add_executable(test_automatic_recovery_events
  "${CMAKE_SOURCE_DIR}/tests/test_automatic_recovery_events.cpp"
  "${CMAKE_SOURCE_DIR}/src/player/AutomaticRecoveryNative.cpp"
  "${CMAKE_SOURCE_DIR}/src/player/AutomaticRecoverySession.cpp"
  "${CMAKE_SOURCE_DIR}/src/player/AutomaticRecoveryQueue.cpp"
  "${CMAKE_SOURCE_DIR}/src/player/AutomaticRecoveryRuntime.cpp")
target_include_directories(test_automatic_recovery_events PRIVATE "${CMAKE_SOURCE_DIR}/src" "${MPV_INCLUDE_DIR}")
target_compile_features(test_automatic_recovery_events PRIVATE cxx_std_17)
target_link_libraries(test_automatic_recovery_events PRIVATE Qt6::Core)
add_test(NAME test_automatic_recovery_events COMMAND test_automatic_recovery_events)
set_tests_properties(test_automatic_recovery_events PROPERTIES TIMEOUT 60)

add_executable(test_automatic_recovery_observation
  "${CMAKE_SOURCE_DIR}/tests/test_automatic_recovery_observation.cpp"
  "${CMAKE_SOURCE_DIR}/src/player/AutomaticRecoveryObservation.cpp"
  "${CMAKE_SOURCE_DIR}/src/player/AutomaticRecoveryNative.cpp")
target_include_directories(test_automatic_recovery_observation PRIVATE "${CMAKE_SOURCE_DIR}/src" "${MPV_INCLUDE_DIR}")
target_compile_features(test_automatic_recovery_observation PRIVATE cxx_std_17)
target_link_libraries(test_automatic_recovery_observation PRIVATE Qt6::Core)
add_test(NAME test_automatic_recovery_observation COMMAND test_automatic_recovery_observation)
set_tests_properties(test_automatic_recovery_observation PROPERTIES TIMEOUT 60)

add_executable(test_automatic_recovery_frame_gate
  "${CMAKE_SOURCE_DIR}/tests/test_automatic_recovery_frame_gate.cpp"
  "${CMAKE_SOURCE_DIR}/src/player/AutomaticRecoveryFrameGate.cpp")
target_include_directories(test_automatic_recovery_frame_gate PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(test_automatic_recovery_frame_gate PRIVATE cxx_std_17)
target_link_libraries(test_automatic_recovery_frame_gate PRIVATE Qt6::Core)
add_test(NAME test_automatic_recovery_frame_gate COMMAND test_automatic_recovery_frame_gate)
set_tests_properties(test_automatic_recovery_frame_gate PROPERTIES TIMEOUT 60)
