# Isolated external-events core. It shares dependencies, never the ordinary core.
function(healthy_restart_verify_wrapper path)
  file(READ "${path}" contents)
  string(REPLACE "\r\n" "\n" contents "${contents}")
  string(SHA256 wrapper_hash "${contents}")
  if(NOT wrapper_hash STREQUAL "802ab8e8c4322e046765b62e6c5772166d5218fa583361c09363d18f8520c14c")
    message(FATAL_ERROR "Healthy restart generated renderer wrapper mismatch")
  endif()
endfunction()

function(healthy_restart_mpvqt)
  set(_mpvqt_target HealthyRestartMpvQt)
  if(ARGC GREATER 0)
    set(_mpvqt_target "${ARGV0}")
  endif()
  set(_mpvqt_src "${_healthy_copy}/mpvqt")
  foreach(output IN ITEMS mpvrenderer-wrapper.cpp MpvController MpvAbstractItem)
    if(IS_SYMLINK "${_mpvqt_src}/${output}")
      message(FATAL_ERROR "Healthy restart generated output must not be a link: ${output}")
    endif()
  endforeach()
  file(WRITE "${_mpvqt_src}/mpvrenderer-wrapper.cpp"
    "#include \"qt-wayland-compat.h\"\n#include \"mpvrenderer.cpp\"\n")
  healthy_restart_verify_wrapper("${_mpvqt_src}/mpvrenderer-wrapper.cpp")
  file(WRITE "${_mpvqt_src}/MpvController" "#include \"mpvcontroller.h\"\n")
  file(WRITE "${_mpvqt_src}/MpvAbstractItem" "#include \"mpvabstractitem.h\"\n")
  add_library(${_mpvqt_target} STATIC
    "${_mpvqt_src}/mpvabstractitem.cpp" "${_mpvqt_src}/mpvcontroller.cpp"
    "${_mpvqt_src}/mpvrenderer-wrapper.cpp" "${_mpvqt_src}/frame_capture.cpp"
    "${_mpvqt_src}/frame_acceptance.cpp" "${_mpvqt_src}/lifetime.cpp"
    "${_mpvqt_src}/lifetime_qt.cpp" "${_mpvqt_src}/owner.cpp" "${_mpvqt_src}/owner_qt.cpp"
    "${_mpvqt_src}/closure.cpp" "${_mpvqt_src}/closure_native.cpp" "${_mpvqt_src}/capture_capability.cpp")
  target_compile_definitions(${_mpvqt_target} PUBLIC MPVQT_EXTERNAL_EVENTS)
  target_include_directories(${_mpvqt_target} PUBLIC "${_mpvqt_src}" "${CMAKE_SOURCE_DIR}/external")
  target_link_libraries(${_mpvqt_target} PUBLIC Qt6::Quick Qt6::Core Qt6::Gui Threads::Threads ${MPV_LIBRARY})
  set_target_properties(${_mpvqt_target} PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC ON)
endfunction()

function(healthy_restart_source_mapping output)
  set(pinned_sources)
  foreach(entry IN LISTS _healthy_input_pins)
    string(REPLACE "|" ";" pair "${entry}")
    list(GET pair 0 source_path)
    list(GET pair 1 target_path)
    if(source_path STREQUAL "src/${target_path}")
      list(APPEND pinned_sources "${target_path}")
    endif()
  endforeach()
  set(copied_sources)
  foreach(source IN LISTS ALL_SRCS)
    file(RELATIVE_PATH relative "${CMAKE_SOURCE_DIR}/src" "${source}")
    set(copy "${_healthy_copy}/${relative}")
    if(relative IN_LIST pinned_sources)
      if(NOT EXISTS "${copy}")
        message(FATAL_ERROR "Healthy restart copied source is missing: ${relative}")
      endif()
      list(APPEND copied_sources "${copy}")
    elseif(relative MATCHES "^(player/|mpris/|input/apple/InputAppleMediaKeys\\.)" OR
           relative MATCHES "^(core/ComponentManager|ui/WindowManager|input/InputComponent|power/PowerComponent)\\.(cpp|h)$" OR
           relative MATCHES "^(taskbar/TaskbarComponentWin|settings/AudioSettingsController)\\.cpp$")
      message(FATAL_ERROR "Healthy restart source lacks a matching pin: ${relative}")
    else()
      list(APPEND copied_sources "${source}")
    endif()
  endforeach()
  set(${output} "${copied_sources}" PARENT_SCOPE)
endfunction()

function(healthy_restart_core)
  set(_core_target HealthyRestartCore)
  set(_linked_mpvqt HealthyRestartMpvQt)
  if(ARGC GREATER 1)
    set(_core_target "${ARGV0}")
    set(_linked_mpvqt "${ARGV1}")
    get_property(ALL_SRCS GLOBAL PROPERTY HEALTHY_RESTART_SOURCE_LIST)
    if(NOT ALL_SRCS)
      message(FATAL_ERROR "Healthy restart source list is unavailable for the test overlay")
    endif()
  else()
    set_property(GLOBAL PROPERTY HEALTHY_RESTART_SOURCE_LIST "${ALL_SRCS}")
  endif()
  healthy_restart_source_mapping(copied_sources)
  foreach(source IN LISTS copied_sources)
    if(source MATCHES "^${_healthy_copy}/")
      file(RELATIVE_PATH relative "${_healthy_copy}" "${source}")
      if(relative MATCHES "^(settings/AudioSettingsController|taskbar/TaskbarComponentWin)\\.cpp$")
        get_filename_component(original_dir "${CMAKE_SOURCE_DIR}/src/${relative}" DIRECTORY)
        set_source_files_properties("${source}" PROPERTIES INCLUDE_DIRECTORIES
          "${_healthy_copy};${_healthy_copy}/player;${_healthy_copy}/core;${original_dir}")
      endif()
    endif()
  endforeach()
  add_library(${_core_target} STATIC ${copied_sources})
  target_sources(${_core_target} PRIVATE
    "${_healthy_copy}/mpvqt/closure_player.cpp" "${_healthy_copy}/mpvqt/closure_wake.cpp"
    "${_healthy_copy}/mpvqt/closure_wake_native.cpp" "${_healthy_copy}/mpvqt/async_stop.cpp"
    "${_healthy_copy}/mpvqt/async_stop_player.cpp" "${_healthy_copy}/mpvqt/module_identity.cpp"
    "${_healthy_copy}/mpvqt/module_identity_win.cpp" "${_healthy_copy}/mpvqt/module_identity_import.cpp"
    "${_healthy_copy}/mpvqt/module_import_prepare.cpp" "${_healthy_copy}/mpvqt/module_gate.cpp"
    "${_healthy_copy}/mpvqt/module_channel.cpp" "${_healthy_copy}/mpvqt/module_endpoints.cpp"
    "${_healthy_copy}/mpvqt/lifetime_module.cpp" "${_healthy_copy}/mpvqt/graph_access.cpp"
    "${_healthy_copy}/mpvqt/graph_capture.cpp"
    "${_healthy_copy}/mpvqt/graph_close.cpp" "${_healthy_copy}/mpvqt/graph_prepare.cpp"
    "${_healthy_copy}/mpvqt/graph_retire.cpp" "${_healthy_copy}/mpvqt/graph_retirement.cpp"
    "${_healthy_copy}/mpvqt/graph_start.cpp" "${_healthy_copy}/mpvqt/graph_session.cpp"
    "${_healthy_copy}/mpvqt/child_session.cpp" "${_healthy_copy}/mpvqt/paused_hold.cpp"
    "${_healthy_copy}/mpvqt/native_unpause_player.cpp" "${_healthy_copy}/mpvqt/native_cleanup_player.cpp"
    "${_healthy_copy}/mpvqt/child_session_grant.cpp" "${_healthy_copy}/mpvqt/child_session_control.cpp"
    "${_healthy_copy}/mpvqt/child_session_failure.cpp" "${_healthy_copy}/mpvqt/resume_progress.cpp"
    "${_healthy_copy}/fixture_restart/FixtureRestartAdapter.cpp"
    "${_healthy_copy}/fixture_restart/FixtureEvidence.cpp" "${_healthy_copy}/fixture_restart/RestartBroker.cpp"
    "${_healthy_copy}/fixture_restart/RestartWire.cpp" "${_healthy_copy}/fixture_restart/RestartChild.cpp"
    "${_healthy_copy}/fixture_restart/RestartChildPlayer.cpp" "${_healthy_copy}/fixture_restart/RestartSource.cpp"
    "${_healthy_copy}/fixture_restart/RestartSourcePlayer.cpp")
  if(WIN32)
    target_sources(${_core_target} PRIVATE "${_healthy_copy}/fixture_restart/WindowsFrameReader.cpp"
      "${_healthy_copy}/fixture_restart/WindowsRestartIo.cpp")
  endif()
  target_compile_definitions(${_core_target} PUBLIC MPVQT_EXTERNAL_EVENTS)
  target_compile_definitions(${_core_target} PRIVATE HEALTHY_RESTART_DIAGNOSTIC)
  target_include_directories(${_core_target} BEFORE PUBLIC "${_healthy_copy}"
    "${_healthy_copy}/player" "${_healthy_copy}/core" "${_healthy_copy}/input"
    "${_healthy_copy}/ui" "${_healthy_copy}/power" "${_healthy_copy}/mpvqt" "${_healthy_copy}/fixture_restart"
    "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/shared")
  get_target_property(core_links jmp_core LINK_LIBRARIES)
  list(FIND core_links MpvQt original_mpvqt)
  if(original_mpvqt EQUAL -1)
    message(FATAL_ERROR "Healthy restart requires the bundled MpvQt dependency")
  endif()
  list(REMOVE_ITEM core_links MpvQt)
  if(WIN32)
    list(APPEND core_links onecore)
  endif()
  foreach(link IN LISTS core_links)
    if(link MATCHES "MpvQt|jmp_core|FixtureCore")
      message(FATAL_ERROR "Healthy restart cannot link a different core: ${link}")
    endif()
  endforeach()
  target_link_libraries(${_core_target} ${core_links} ${_linked_mpvqt})
  configure_windows_loader(${_core_target} "${_healthy_copy}/player/WindowsMpvLoader.cpp")
endfunction()

function(healthy_restart_test_overlay)
  healthy_restart_mpvqt(HealthyGraphTestMpvQt)
  healthy_restart_core(HealthyGraphTestCore HealthyGraphTestMpvQt)
  set(_graph_test_definitions HEALTHY_RESTART_DIAGNOSTIC HEALTHY_RETIREMENT_TESTING
    HEALTHY_MODULE_TESTING HEALTHY_ENDPOINT_TESTING)
  target_compile_definitions(HealthyGraphTestMpvQt PRIVATE ${_graph_test_definitions})
  target_compile_definitions(HealthyGraphTestCore PRIVATE ${_graph_test_definitions})
  add_executable(healthy_graph_capture test_healthy_graph_capture.cpp)
  target_compile_definitions(healthy_graph_capture PRIVATE ${_graph_test_definitions})
  target_link_libraries(healthy_graph_capture PRIVATE HealthyGraphTestCore Qt6::Test)
  add_test(NAME healthy_graph_capture COMMAND healthy_graph_capture)
  set_tests_properties(healthy_graph_capture PROPERTIES TIMEOUT 60)
  if(WIN32)
    add_executable(healthy_graph_arm test_healthy_graph_arm.cpp)
    target_compile_definitions(healthy_graph_arm PRIVATE ${_graph_test_definitions})
    target_link_libraries(healthy_graph_arm PRIVATE HealthyGraphTestCore Qt6::Test)
    add_test(NAME healthy_graph_arm COMMAND healthy_graph_arm)
    set_tests_properties(healthy_graph_arm PROPERTIES TIMEOUT 60)
    add_executable(healthy_graph_failure_provenance test_healthy_failure_provenance.cpp)
    target_compile_definitions(healthy_graph_failure_provenance PRIVATE ${_graph_test_definitions})
    target_link_libraries(healthy_graph_failure_provenance PRIVATE HealthyGraphTestCore Qt6::Test)
    add_test(NAME healthy_graph_failure_provenance COMMAND healthy_graph_failure_provenance)
    set_tests_properties(healthy_graph_failure_provenance PROPERTIES TIMEOUT 60)
  endif()
endfunction()

function(healthy_restart_tests)
  healthy_restart_test_overlay()
  set(_graph_test_definitions HEALTHY_RESTART_DIAGNOSTIC HEALTHY_RETIREMENT_TESTING
    HEALTHY_MODULE_TESTING HEALTHY_ENDPOINT_TESTING)
  add_executable(test_healthy_restart EXCLUDE_FROM_ALL healthy_restart_held.cpp)
  target_link_libraries(test_healthy_restart PRIVATE HealthyRestartCore Qt6::Core)
  add_executable(healthy_async_stop_player test_healthy_async_stop_player.cpp)
  target_link_libraries(healthy_async_stop_player PRIVATE HealthyRestartCore Qt6::Test)
  add_test(NAME healthy_async_stop_player COMMAND healthy_async_stop_player)
  set_tests_properties(healthy_async_stop_player PROPERTIES TIMEOUT 60)
  add_executable(healthy_unpause_player test_healthy_unpause_player.cpp)
  target_compile_definitions(healthy_unpause_player PRIVATE ${_graph_test_definitions})
  target_link_libraries(healthy_unpause_player PRIVATE HealthyGraphTestCore Qt6::Test)
  add_test(NAME healthy_unpause_player COMMAND healthy_unpause_player)
  set_tests_properties(healthy_unpause_player PROPERTIES TIMEOUT 60)
  foreach(name IN ITEMS fixture_adapter_cleanup healthy_restore_cancel)
    add_executable(healthy_${name} "test_${name}.cpp")
    target_compile_definitions(healthy_${name} PRIVATE ${_graph_test_definitions})
    target_link_libraries(healthy_${name} PRIVATE HealthyGraphTestCore Qt6::Test)
    add_test(NAME healthy_${name} COMMAND healthy_${name})
    set_tests_properties(healthy_${name} PROPERTIES TIMEOUT 60)
  endforeach()
  add_executable(healthy_async_stop_source "${CMAKE_SOURCE_DIR}/tests/healthy_restart/test_async_stop_source.cpp"
    "${_healthy_copy}/fixture_restart/RestartSource.cpp" "${_healthy_copy}/fixture_restart/RestartWire.cpp"
    "${_healthy_copy}/fixture_restart/RestartBroker.cpp" "${_healthy_copy}/fixture_restart/FixtureEvidence.cpp"
    "${_healthy_copy}/player/PlaybackCheckpoint.cpp" "${_healthy_copy}/player/PlaybackIntent.cpp")
  target_include_directories(healthy_async_stop_source BEFORE PRIVATE "${_healthy_copy}"
    "${_healthy_copy}/fixture_restart" "${_healthy_copy}/mpvqt")
  target_link_libraries(healthy_async_stop_source PRIVATE Qt6::Core)
  add_test(NAME healthy_async_stop_source COMMAND healthy_async_stop_source)
  set_tests_properties(healthy_async_stop_source PROPERTIES TIMEOUT 60)
  foreach(name IN ITEMS player_native_receipts player_restore_cleanup player_restore_observation)
    add_executable(healthy_${name} "test_${name}.cpp")
    if(name STREQUAL "player_restore_cleanup")
      target_compile_definitions(healthy_${name} PRIVATE HEALTHY_COPIED_SOURCE_TEST ${_graph_test_definitions})
      target_link_libraries(healthy_${name} PRIVATE HealthyGraphTestCore Qt6::Test)
    else()
      target_link_libraries(healthy_${name} PRIVATE HealthyRestartCore Qt6::Test)
    endif()
    add_test(NAME healthy_${name} COMMAND healthy_${name})
    set_tests_properties(healthy_${name} PROPERTIES TIMEOUT 60)
  endforeach()
  add_executable(healthy_player_native_restore test_player_native_restore.cpp test_player_native_restore.h
    test_player_native_restore_capture.cpp test_player_native_restore_bridge.cpp test_player_native_restore_healthy.cpp)
  target_compile_definitions(healthy_player_native_restore PRIVATE HEALTHY_COPIED_SOURCE_TEST ${_graph_test_definitions})
  target_link_libraries(healthy_player_native_restore PRIVATE HealthyGraphTestCore Qt6::Test)
  add_test(NAME healthy_player_native_restore COMMAND healthy_player_native_restore)
  set_tests_properties(healthy_player_native_restore PROPERTIES TIMEOUT 60)
  # This headless controller test creates no playback, renderer or restart process.
  add_executable(healthy_mpvqt_event_ownership test_mpvqt_event_ownership.cpp mpv_test_bootstrap.cpp)
  target_link_libraries(healthy_mpvqt_event_ownership PRIVATE HealthyRestartMpvQt Qt6::Test)
  add_test(NAME healthy_mpvqt_event_ownership COMMAND healthy_mpvqt_event_ownership)
  set_tests_properties(healthy_mpvqt_event_ownership PROPERTIES TIMEOUT 60)
  # These existing GUI/controller checks remain build-only with the held fixture.
  foreach(name IN ITEMS mpvqt_player_binding player_released_state)
    add_executable(healthy_${name} EXCLUDE_FROM_ALL "test_${name}.cpp" mpv_test_bootstrap.cpp
      mpv_test_profile.cpp "${CMAKE_BINARY_DIR}/src/qrc_resources.cpp")
    add_dependencies(healthy_${name} generate_resources)
    target_link_libraries(healthy_${name} PRIVATE HealthyRestartCore Qt6::Test Qt6::Quick)
  endforeach()
endfunction()
