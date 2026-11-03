find_program(ISCC iscc
  PATHS "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6"
    "$ENV{ProgramFiles\(x86\)}/Inno Setup 6" "$ENV{ProgramFiles}/Inno Setup 6"
    "C:/Program Files (x86)/Inno Setup 6" "C:/Program Files/Inno Setup 6"
  DOC "Inno Setup Compiler")
if(NOT ISCC)
  message(WARNING "Inno Setup Compiler (iscc) not found. Windows installer will not be built.")
  return()
endif()

if(CMAKE_SIZEOF_VOID_P MATCHES 8)
  set(INSTALLER_ARCH_STR x64)
else()
  set(INSTALLER_ARCH_STR x86)
endif()
set(INSTALLER_BASE_NAME "JellyfinDesktop-${VERSION_STRING}-${INSTALLER_ARCH_STR}")
set(PACKAGE_WORK "${CMAKE_CURRENT_BINARY_DIR}/windows-package-work")
set(PACKAGE_STAGE "${PACKAGE_WORK}/stage")
set(PACKAGE_PORTABLE "${PACKAGE_WORK}/portable")
set(PACKAGE_TEMP "${PACKAGE_WORK}/temporary")
set(PACKAGE_INSTALLER "${CMAKE_CURRENT_BINARY_DIR}/${INSTALLER_BASE_NAME}.exe")
set(PACKAGE_ZIP "${CMAKE_CURRENT_BINARY_DIR}/${INSTALLER_BASE_NAME}.zip")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/windows-package-outputs.txt"
  "${INSTALLER_BASE_NAME}.exe\n${INSTALLER_BASE_NAME}.zip\n")

# The Inno script must reference the same fresh stage as the ZIP.
set(_original_install_prefix "${CMAKE_INSTALL_PREFIX}")
set(CMAKE_INSTALL_PREFIX "${PACKAGE_STAGE}")
configure_file("${PROJECT_SOURCE_DIR}/bundle/win/JellyfinDesktop.iss.in"
  "${CMAKE_CURRENT_BINARY_DIR}/JellyfinDesktop.iss" @ONLY)
set(CMAKE_INSTALL_PREFIX "${_original_install_prefix}")
configure_file("${PROJECT_SOURCE_DIR}/CMakeModules/WindowsPackageStage.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/WindowsPackageStage.cmake" @ONLY)
configure_file("${PROJECT_SOURCE_DIR}/CMakeModules/WindowsPackagePublish.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/WindowsPackagePublish.cmake" @ONLY)

# A requested package always starts from one fresh build-owned stage. This
# shared target runs once when windows_all requests both package variants.
add_custom_target(innosetup_install
  COMMAND "${CMAKE_COMMAND}" "-DPACKAGE_CONFIG=$<CONFIG>" -P "${CMAKE_CURRENT_BINARY_DIR}/WindowsPackageStage.cmake"
  DEPENDS JellyfinDesktop
  COMMENT "Staging fresh Windows package files" VERBATIM)

add_custom_target(JellyfinDesktopInstaller
  COMMAND "${CMAKE_COMMAND}" -DPACKAGE_KIND=installer -P "${CMAKE_CURRENT_BINARY_DIR}/WindowsPackagePublish.cmake"
  DEPENDS innosetup_install
  BYPRODUCTS "${PACKAGE_INSTALLER}"
  COMMENT "Building Windows installer" VERBATIM)
add_custom_target(windows_package DEPENDS JellyfinDesktopInstaller)

add_custom_target(JellyfinDesktopZip
  COMMAND "${CMAKE_COMMAND}" -DPACKAGE_KIND=zip -P "${CMAKE_CURRENT_BINARY_DIR}/WindowsPackagePublish.cmake"
  DEPENDS innosetup_install
  BYPRODUCTS "${PACKAGE_ZIP}"
  COMMENT "Building Windows portable ZIP" VERBATIM)
add_custom_target(windows_zip DEPENDS JellyfinDesktopZip)
add_custom_target(windows_all DEPENDS JellyfinDesktopInstaller JellyfinDesktopZip)
