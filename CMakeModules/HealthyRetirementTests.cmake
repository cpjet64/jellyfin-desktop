# Headless tests of the diagnostic retirement protocol and its Qt adapter.
find_package(Threads REQUIRED)
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
