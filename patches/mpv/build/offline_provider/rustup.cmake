# Overlay for provider blob 088936428c2a9a552bb9f2dd8969ba70b3bd9f7a.
# A future admission controller must verify the complete staged toolchain and
# its lock before invoking CMake. This target deliberately performs no setup.
if(NOT DEFINED OFFLINE_RUST_ROOT OR NOT IS_ABSOLUTE "${OFFLINE_RUST_ROOT}")
    message(FATAL_ERROR "OFFLINE_RUST_ROOT must name the verified staged Rust toolchain")
endif()
if(NOT EXISTS "${OFFLINE_RUST_ROOT}/bin/rustc" OR
   NOT EXISTS "${OFFLINE_RUST_ROOT}/bin/cargo" OR
   NOT EXISTS "${OFFLINE_RUST_ROOT}/lib/rustlib/x86_64-pc-windows-gnu")
    message(FATAL_ERROR "The staged Rust host and Windows GNU target are incomplete")
endif()
add_custom_target(rustup)
