# Ammonite plugin: VST3 + CLAP for Windows x64 (DPF). Built by
# .\plugin\plugin.ps1 build (MSVC + CMake + Ninja from VS Build Tools 2022).
cmake_minimum_required(VERSION 3.15)

# Static C runtime: the plugin needs no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
cmake_policy(SET CMP0091 NEW)

project(Ammonite VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(lib/DPF)

set(DAISYSP ${CMAKE_CURRENT_SOURCE_DIR}/../lib/DaisySP/Source)

dpf_add_plugin(Ammonite
  TARGETS vst3 clap
  FILES_DSP
    src/AmmonitePlugin.cpp
    engine/engine.cpp
    engine/reverbsc.cpp
    ${DAISYSP}/Synthesis/oscillator.cpp
    ${DAISYSP}/Filters/svf.cpp
    ${DAISYSP}/Control/phasor.cpp
  FILES_UI
    src/AmmoniteUI.cpp)

target_include_directories(Ammonite PUBLIC
  src
  engine
  ${DAISYSP}
  ${DAISYSP}/Synthesis
  ${DAISYSP}/Filters
  ${DAISYSP}/Utility
  ${DAISYSP}/Control)

# The engine is the hardware's C++14 code: keep MSVC quiet about its idioms
# (float/double conversions, fopen-style warnings in DaisySP) and give it
# M_PI, which DaisySP's headers expect.
target_compile_definitions(Ammonite PUBLIC _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
if(MSVC)
  target_compile_options(Ammonite PUBLIC /W3 /wd4244 /wd4305 /wd4267 /fp:precise)
endif()
