if(USE_STATIC_MPVQT AND MPVQT_EXTERNAL_EVENTS AND NOT MPVQT_C2_DIAGNOSTIC_ONLY)
  get_target_property(_recovery_actor_sources jmp_core SOURCES)
  add_library(recovery_actor_test_core STATIC ${_recovery_actor_sources})
  set_target_properties(recovery_actor_test_core PROPERTIES AUTOMOC ON)
  target_compile_features(recovery_actor_test_core PRIVATE cxx_std_17)
  target_compile_definitions(recovery_actor_test_core PUBLIC PLAYER_RECOVERY_ACTOR_TEST
    $<TARGET_PROPERTY:jmp_core,COMPILE_DEFINITIONS>)
  target_include_directories(recovery_actor_test_core PRIVATE $<TARGET_PROPERTY:jmp_core,INCLUDE_DIRECTORIES>)
  target_link_libraries(recovery_actor_test_core PUBLIC $<TARGET_PROPERTY:jmp_core,LINK_LIBRARIES>)
  configure_windows_loader(recovery_actor_test_core "${CMAKE_SOURCE_DIR}/src/player/WindowsMpvLoader.cpp")
  add_executable(test_recovery_actor
    "${CMAKE_SOURCE_DIR}/tests/test_recovery_actor.cpp"
    "${CMAKE_SOURCE_DIR}/tests/recovery_actor/ActorTest.h"
    "${CMAKE_SOURCE_DIR}/tests/recovery_actor/TimingCases.cpp")
  target_include_directories(test_recovery_actor PRIVATE
    "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/core"
    "${CMAKE_SOURCE_DIR}/patches/mpvqt/runtime" "${MPV_INCLUDE_DIR}")
  target_compile_features(test_recovery_actor PRIVATE cxx_std_17)
  target_link_libraries(test_recovery_actor PRIVATE recovery_actor_test_core Qt6::Test)
  add_test(NAME test_recovery_actor COMMAND test_recovery_actor)
  set_tests_properties(test_recovery_actor PROPERTIES TIMEOUT 60)
endif()
