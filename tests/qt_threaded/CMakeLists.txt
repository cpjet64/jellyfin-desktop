find_package(Qt6 6.9.3 EXACT REQUIRED COMPONENTS Core Gui Quick OpenGL)
if(NOT TARGET Qt6::QuickPrivate OR NOT TARGET Qt6::GuiPrivate)
  message(FATAL_ERROR "Qt 6.9.3 matching QuickPrivate and GuiPrivate targets are required")
endif()
add_library(qt_threaded_fixture STATIC helper.cpp cleanup.cpp)
target_include_directories(qt_threaded_fixture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qt_threaded_fixture PUBLIC Qt6::Core Qt6::Gui Qt6::Quick Qt6::OpenGL
                                      PRIVATE Qt6::QuickPrivate Qt6::GuiPrivate)
