# Test-only fixture sources share the same copied Graph and Player headers as the overlay.
function(healthy_restart_add_restore_fixture target)
  target_sources(${target} PRIVATE
    "${_healthy_copy}/mpvqt/restore_test_fixture.cpp"
    "${_healthy_copy}/mpvqt/restore_test_events.cpp"
    "${_healthy_copy}/mpvqt/restore_test_renderer.cpp")
endfunction()
