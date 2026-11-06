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
