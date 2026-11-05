# The secure delay-load hook resolves mpv beside the consuming executable.
# Stage developer/test layouts just as the distributable stages its DLLs.
get_filename_component(_mpv_import "${MPV_LIBRARY}" ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
get_filename_component(_mpv_runtime_dir "${_mpv_import}" DIRECTORY)
set(_mpv_fallback "${_mpv_runtime_dir}/../mpv-fallback/libmpv-2.dll")
add_custom_target(windows_mpv_runtime ALL)
set(_mpv_destinations "$<TARGET_FILE_DIR:test_log>")
set(_mpv_consumers test_systemcomponent test_log test_settings test_displaymanager test_windowmanager test_log_rotation)
if(TARGET ${MAIN_TARGET})
  list(APPEND _mpv_destinations "$<TARGET_FILE_DIR:${MAIN_TARGET}>")
  list(APPEND _mpv_consumers ${MAIN_TARGET})
endif()
foreach(_destination IN LISTS _mpv_destinations)
  add_custom_command(TARGET windows_mpv_runtime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_destination}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_mpv_runtime_dir}/libmpv-2.dll" "${_destination}/libmpv-2.dll"
    VERBATIM)
  if(EXISTS "${_mpv_fallback}")
    add_custom_command(TARGET windows_mpv_runtime POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_mpv_fallback}" "${_destination}/libmpv-fallback.dll"
      VERBATIM)
  endif()
endforeach()
foreach(_consumer IN LISTS _mpv_consumers)
  add_dependencies(${_consumer} windows_mpv_runtime)
endforeach()
