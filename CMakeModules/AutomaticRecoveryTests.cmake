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
target_link_libraries(test_automatic_recovery_frame_gate PRIVATE Qt6::Core Qt6::Gui Qt6::Quick)
add_test(NAME test_automatic_recovery_frame_gate COMMAND test_automatic_recovery_frame_gate)
set_tests_properties(test_automatic_recovery_frame_gate PROPERTIES TIMEOUT 60)

add_executable(test_automatic_recovery_read_stall
  "${CMAKE_SOURCE_DIR}/tests/test_automatic_recovery_read_stall.cpp"
  "${CMAKE_SOURCE_DIR}/src/player/AutomaticRecoveryReadStall.cpp")
target_include_directories(test_automatic_recovery_read_stall PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(test_automatic_recovery_read_stall PRIVATE cxx_std_17)
target_link_libraries(test_automatic_recovery_read_stall PRIVATE Qt6::Core)
add_test(NAME test_automatic_recovery_read_stall COMMAND test_automatic_recovery_read_stall)
set_tests_properties(test_automatic_recovery_read_stall PROPERTIES TIMEOUT 60)

add_executable(test_automatic_recovery_owned_seek
  "${CMAKE_SOURCE_DIR}/tests/test_automatic_recovery_owned_seek.cpp"
  "${CMAKE_SOURCE_DIR}/src/player/AutomaticRecoveryOwnedSeek.cpp")
target_include_directories(test_automatic_recovery_owned_seek PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(test_automatic_recovery_owned_seek PRIVATE cxx_std_17)
target_link_libraries(test_automatic_recovery_owned_seek PRIVATE Qt6::Core)
add_test(NAME test_automatic_recovery_owned_seek COMMAND test_automatic_recovery_owned_seek)
set_tests_properties(test_automatic_recovery_owned_seek PROPERTIES TIMEOUT 60)

if(USE_STATIC_MPVQT AND MPVQT_EXTERNAL_EVENTS AND NOT MPVQT_C2_DIAGNOSTIC_ONLY)
  add_executable(test_mpvqt_recovery_exchange "${CMAKE_SOURCE_DIR}/tests/test_mpvqt_recovery_exchange.cpp")
  target_include_directories(test_mpvqt_recovery_exchange PRIVATE "${CMAKE_SOURCE_DIR}/patches/mpvqt/runtime" "${MPV_INCLUDE_DIR}")
  target_compile_features(test_mpvqt_recovery_exchange PRIVATE cxx_std_17)
  target_link_libraries(test_mpvqt_recovery_exchange PRIVATE MpvQt Qt6::Core Qt6::Test)
  add_test(NAME test_mpvqt_recovery_exchange COMMAND test_mpvqt_recovery_exchange)
  set_tests_properties(test_mpvqt_recovery_exchange PROPERTIES TIMEOUT 60)
  add_executable(test_renderer_admission "${CMAKE_SOURCE_DIR}/tests/test_renderer_admission.cpp")
  target_include_directories(test_renderer_admission PRIVATE "${CMAKE_SOURCE_DIR}/patches/mpvqt/runtime" "${MPV_INCLUDE_DIR}")
  target_compile_features(test_renderer_admission PRIVATE cxx_std_17)
  target_link_libraries(test_renderer_admission PRIVATE MpvQt Qt6::Core Qt6::Test)
  foreach(case delayedFinalizationReservesOnce cancelledWaiterIsNotCalled completionBeforeRegistrationAllowsReservation quarantineWinsAdmission failedReservationRetiresBeforeReadmission internalWakeBeforeBindingIsDrained sameCoreConsumerCanRebindAndDrain)
    add_test(NAME renderer_admission_${case} COMMAND test_renderer_admission ${case})
    set_tests_properties(renderer_admission_${case} PROPERTIES TIMEOUT 60)
  endforeach()
endif()
