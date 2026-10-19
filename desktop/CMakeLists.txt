# Ammonite Desktop, phase 1: the test plugin.
# Two variants from one source tree, built together by .\desktop\desktop.ps1 build:
#   AmmoniteTestWeb  the UI is a WebView2 page (Canvas 2D knobs + WebGL shell)
#   AmmoniteTestGL   the UI is NanoVG knobs + a raw OpenGL shell
# Both are VST3 + CLAP for Windows x64 through DPF (MSVC + CMake + Ninja).
cmake_minimum_required(VERSION 3.15)

# Static C runtime: no Visual C++ redistributable needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
cmake_policy(SET CMP0091 NEW)

project(AmmoniteDesktop VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(lib/DPF)

set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)
set(GEN_DIR ${CMAKE_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN_DIR})

# The web page goes into the binary as a byte array (a data: URL at runtime;
# AMMO_WEB_DIR=<dir> loads <dir>/index.html from disk instead, for editing).
add_custom_command(
  OUTPUT ${GEN_DIR}/IndexHtml.hpp
  COMMAND ${CMAKE_COMMAND}
          -DIN=${TEST_DIR}/web/index.html
          -DOUT=${GEN_DIR}/IndexHtml.hpp
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_html.cmake
  DEPENDS ${TEST_DIR}/web/index.html ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_html.cmake
  COMMENT "Embedding test/web/index.html")

# ---- web variant: DPF UI + CHOC WebView2 (USE_WEB_VIEW compiles DPF's Win32 web view glue)
dpf_add_plugin(AmmoniteTestWeb
  TARGETS vst3 clap
  USE_WEB_VIEW
  FILES_DSP
    test/TestPlugin.cpp
  FILES_UI
    test/TestUIWeb.cpp
    ${GEN_DIR}/IndexHtml.hpp)
target_include_directories(AmmoniteTestWeb PUBLIC test ${GEN_DIR})
target_compile_definitions(AmmoniteTestWeb PUBLIC AMMO_UI_WEB=1)

# ---- native variant: DPF UI, NanoVG + raw OpenGL 2 (GLSL 1.20)
dpf_add_plugin(AmmoniteTestGL
  TARGETS vst3 clap
  FILES_DSP
    test/TestPlugin.cpp
  FILES_UI
    test/TestUIGL.cpp)
target_include_directories(AmmoniteTestGL PUBLIC test)
target_compile_definitions(AmmoniteTestGL PUBLIC AMMO_UI_GL=1)
target_link_libraries(AmmoniteTestGL-ui PRIVATE opengl32)

# ---- the engine (phase 2): a static library, and a DLL with the C API for the tests
add_library(ammonite_engine_lib STATIC engine/Engine.cpp)
target_include_directories(ammonite_engine_lib PUBLIC engine)
target_compile_definitions(ammonite_engine_lib PUBLIC _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS NOMINMAX)
add_library(ammonite_engine SHARED engine/api.cpp)
target_link_libraries(ammonite_engine PRIVATE ammonite_engine_lib)
set_target_properties(ammonite_engine PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<0:>"
  PREFIX "")
if(MSVC)
  target_compile_options(ammonite_engine_lib PUBLIC /W3 /fp:precise)
endif()

# ---- the product (phase 3): Ammonite Desktop = DPF + the engine + the web UI
set(PLUGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/plugin)
set(GEN_PLUGIN_DIR ${CMAKE_BINARY_DIR}/gen/plugin)
file(MAKE_DIRECTORY ${GEN_PLUGIN_DIR})
add_custom_command(
  OUTPUT ${GEN_PLUGIN_DIR}/IndexHtml.hpp
  COMMAND ${CMAKE_COMMAND}
          -DDIR=${PLUGIN_DIR}/web
          -DOUT=${GEN_PLUGIN_DIR}/IndexHtml.hpp
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundle_web.cmake
  DEPENDS ${PLUGIN_DIR}/web/index.html ${PLUGIN_DIR}/web/app.css ${PLUGIN_DIR}/web/knobs.js
          ${PLUGIN_DIR}/web/shells.js ${PLUGIN_DIR}/web/app.js ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundle_web.cmake
  COMMENT "Bundling plugin/web into the plugin")

dpf_add_plugin(AmmoniteDesktop
  TARGETS vst3 clap
  USE_WEB_VIEW
  FILES_DSP
    plugin/AmmonitePlugin.cpp
  FILES_UI
    plugin/AmmoniteUI.cpp
    ${GEN_PLUGIN_DIR}/IndexHtml.hpp)
target_include_directories(AmmoniteDesktop PUBLIC plugin engine ${GEN_PLUGIN_DIR})
target_link_libraries(AmmoniteDesktop-dsp PRIVATE ammonite_engine_lib)
target_link_libraries(AmmoniteDesktop-ui PRIVATE ammonite_engine_lib)
target_compile_definitions(AmmoniteDesktop PUBLIC _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS NOMINMAX)
if(MSVC)
  target_compile_options(AmmoniteDesktop PUBLIC /W3 /wd4244 /wd4305 /wd4267 /fp:precise)
endif()

# ---- the standalone host: loads either .clap, plays it, runs a transport (no DAW needed)
add_executable(AmmoniteTestHost WIN32 host/StandaloneHost.cpp)
target_include_directories(AmmoniteTestHost PRIVATE lib/DPF/distrho/src)
target_compile_definitions(AmmoniteTestHost PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX UNICODE _UNICODE)
target_link_libraries(AmmoniteTestHost PRIVATE ole32 avrt user32 gdi32 shell32)
set_target_properties(AmmoniteTestHost PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<0:>")
if(MSVC)
  target_compile_options(AmmoniteTestHost PRIVATE /W3)
endif()

foreach(t AmmoniteTestWeb AmmoniteTestGL)
  target_compile_definitions(${t} PUBLIC _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS NOMINMAX)
  if(MSVC)
    target_compile_options(${t} PUBLIC /W3 /wd4244 /wd4305 /wd4267 /fp:precise)
  endif()
endforeach()
