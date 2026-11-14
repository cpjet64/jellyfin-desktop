add_executable(test_automatic_playback_recovery
  "${CMAKE_SOURCE_DIR}/tests/test_automatic_playback_recovery.cpp"
  "${CMAKE_SOURCE_DIR}/src/player/AutomaticRecoveryPolicy.cpp")
target_include_directories(test_automatic_playback_recovery PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(test_automatic_playback_recovery PRIVATE cxx_std_17)
add_test(NAME test_automatic_playback_recovery COMMAND test_automatic_playback_recovery)
set_tests_properties(test_automatic_playback_recovery PROPERTIES TIMEOUT 60)
