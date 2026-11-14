# Overlay for provider blob 0725925d8af8fe1c6f38cda40d8e6c36944d3a72.
# The staged, patched module and its included files require lock verification
# before CMake starts. This file performs no download, extraction, or patching.
if(NOT DEFINED OFFLINE_EXTERNALPROJECT_MODULE OR
   NOT IS_ABSOLUTE "${OFFLINE_EXTERNALPROJECT_MODULE}" OR
   NOT EXISTS "${OFFLINE_EXTERNALPROJECT_MODULE}")
    message(FATAL_ERROR "A verified staged ExternalProject module is required")
endif()
include("${OFFLINE_EXTERNALPROJECT_MODULE}")
