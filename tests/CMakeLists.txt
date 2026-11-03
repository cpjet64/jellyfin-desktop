cmake_minimum_required(VERSION 3.10...3.31)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 ${REQUIRED_QT_VERSION} REQUIRED COMPONENTS Test)

if(MPVQT_EXTERNAL_EVENTS)
  add_executable(test_mpvqt_event_ownership test_mpvqt_event_ownership.cpp)
  target_link_libraries(test_mpvqt_event_ownership MpvQt Qt6::Test)
  add_test(NAME test_mpvqt_event_ownership COMMAND test_mpvqt_event_ownership)
  set_tests_properties(test_mpvqt_event_ownership PROPERTIES TIMEOUT 60)
endif()

set(TEST_INCLUDE_DIRS
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/shared
  ${CMAKE_SOURCE_DIR}/external
  ${CMAKE_SOURCE_DIR}/src/player
)

set(TEST_COMPILE_DEFS
  PREFIX="${CMAKE_INSTALL_PREFIX}"
)

add_executable(test_systemcomponent test_systemcomponent.cpp)
target_link_libraries(test_systemcomponent jmp_core Qt6::Test)
target_include_directories(test_systemcomponent PRIVATE ${TEST_INCLUDE_DIRS})
target_compile_definitions(test_systemcomponent PRIVATE ${TEST_COMPILE_DEFS})
add_test(NAME test_systemcomponent COMMAND test_systemcomponent)

add_executable(test_log test_log.cpp)
target_link_libraries(test_log jmp_core Qt6::Test)
target_include_directories(test_log PRIVATE ${TEST_INCLUDE_DIRS})
target_compile_definitions(test_log PRIVATE ${TEST_COMPILE_DEFS})
add_test(NAME test_log COMMAND test_log)

add_executable(test_settings test_settings.cpp)
target_link_libraries(test_settings jmp_core Qt6::Test)
target_include_directories(test_settings PRIVATE ${TEST_INCLUDE_DIRS})
target_compile_definitions(test_settings PRIVATE ${TEST_COMPILE_DEFS})
add_test(NAME test_settings COMMAND test_settings)

add_executable(test_displaymanager test_displaymanager.cpp)
target_link_libraries(test_displaymanager jmp_core Qt6::Test)
target_include_directories(test_displaymanager PRIVATE ${TEST_INCLUDE_DIRS})
target_compile_definitions(test_displaymanager PRIVATE ${TEST_COMPILE_DEFS})
add_test(NAME test_displaymanager COMMAND test_displaymanager)

foreach(test_name test_windowmanager test_log_rotation test_fault_stop)
  add_executable(${test_name} ${test_name}.cpp)
  target_link_libraries(${test_name} jmp_core Qt6::Test)
  target_include_directories(${test_name} PRIVATE ${TEST_INCLUDE_DIRS})
  target_compile_definitions(${test_name} PRIVATE ${TEST_COMPILE_DEFS})
  add_test(NAME ${test_name} COMMAND ${test_name})
  set_tests_properties(${test_name} PROPERTIES TIMEOUT 60)
endforeach()
set_tests_properties(test_windowmanager PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(test_fault_stop PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_chromiumflags test_chromiumflags.cpp)
target_link_libraries(test_chromiumflags Qt6::Test)
target_include_directories(test_chromiumflags PRIVATE ${TEST_INCLUDE_DIRS})
add_test(NAME test_chromiumflags COMMAND test_chromiumflags)

add_executable(test_webclient_fault_guard test_webclient_fault_guard.cpp)
target_link_libraries(test_webclient_fault_guard jmp_core Qt6::Test Qt6::WebChannel)
target_include_directories(test_webclient_fault_guard PRIVATE ${TEST_INCLUDE_DIRS})
target_compile_definitions(test_webclient_fault_guard PRIVATE ${TEST_COMPILE_DEFS})
add_test(NAME test_webclient_fault_guard COMMAND test_webclient_fault_guard)
set_tests_properties(test_webclient_fault_guard PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_webclient_transport test_webclient_transport.cpp)
target_link_libraries(test_webclient_transport Qt6::Test Qt6::WebChannel)
add_test(NAME test_webclient_transport COMMAND test_webclient_transport)
set_tests_properties(test_webclient_transport PROPERTIES TIMEOUT 60)

add_executable(test_webclient_fault_webengine test_webclient_fault_webengine.cpp)
target_link_libraries(test_webclient_fault_webengine jmp_core Qt6::Test Qt6::Quick Qt6::WebEngineQuick Qt6::WebChannel)
target_include_directories(test_webclient_fault_webengine PRIVATE ${TEST_INCLUDE_DIRS})
target_compile_definitions(test_webclient_fault_webengine PRIVATE ${TEST_COMPILE_DEFS} TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME test_webclient_fault_webengine COMMAND test_webclient_fault_webengine)
set_tests_properties(test_webclient_fault_webengine PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS "webengine;gui")

add_executable(test_renderer_loss_playback test_renderer_loss_playback.cpp)
target_sources(test_renderer_loss_playback PRIVATE "${CMAKE_BINARY_DIR}/src/qrc_resources.cpp")
add_dependencies(test_renderer_loss_playback generate_resources)
target_link_libraries(test_renderer_loss_playback jmp_core Qt6::Test Qt6::Quick)
target_include_directories(test_renderer_loss_playback PRIVATE ${TEST_INCLUDE_DIRS})
target_compile_definitions(test_renderer_loss_playback PRIVATE ${TEST_COMPILE_DEFS})
# Run each scenario in an isolated native desktop process with RECOVERY_* inputs.

add_executable(test_webclient_containment test_webclient_containment.cpp
  "${CMAKE_BINARY_DIR}/src/qrc_resources.cpp")
add_dependencies(test_webclient_containment generate_resources)
target_link_libraries(test_webclient_containment jmp_core Qt6::Test Qt6::Quick Qt6::WebEngineQuick Qt6::WebChannel)
target_include_directories(test_webclient_containment PRIVATE ${TEST_INCLUDE_DIRS})
target_compile_definitions(test_webclient_containment PRIVATE ${TEST_COMPILE_DEFS} TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
# CI explicitly runs all three scenarios in isolated native desktop processes.

if(LINUX_DBUS)
  add_executable(test_mpris_fault test_mpris_fault.cpp)
  target_link_libraries(test_mpris_fault jmp_core Qt6::Test Qt6::DBus)
  target_include_directories(test_mpris_fault PRIVATE ${TEST_INCLUDE_DIRS})
  target_compile_definitions(test_mpris_fault PRIVATE ${TEST_COMPILE_DEFS})
  add_test(NAME test_mpris_fault COMMAND test_mpris_fault)
  set_tests_properties(test_mpris_fault PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
