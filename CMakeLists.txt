cmake_minimum_required(VERSION 3.25)
project(T9Ime VERSION 0.1.0 LANGUAGES CXX)

if(NOT MSVC)
  message(FATAL_ERROR "T9Ime builds with MSVC only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT everywhere (SPEC: no VC++ runtime dependency).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(T9_ARCH x64)
else()
  set(T9_ARCH x86)
endif()

# Windows 7 SP1 is the minimum target; newer APIs must be loaded dynamically.
add_compile_definitions(_WIN32_WINNT=0x0601 WINVER=0x0601 NTDDI_VERSION=0x06010000
                        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc)

# Mark an executable as runnable on Windows 7 (subsystem version 6.01).
function(t9_win7_subsystem target kind)
  target_link_options(${target} PRIVATE "/SUBSYSTEM:${kind},6.01")
endfunction()

# Executables, rime.dll and data share one directory, like the installed layout.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ---- librime (official prebuilt, fetched by third_party/fetch_librime.ps1) ----
set(T9_LIBRIME_DIR "${CMAKE_SOURCE_DIR}/third_party/librime/${T9_ARCH}")
if(NOT EXISTS "${T9_LIBRIME_DIR}/lib/rime.lib")
  message(FATAL_ERROR "librime not found: run third_party/fetch_librime.ps1")
endif()
add_library(rime SHARED IMPORTED GLOBAL)
set_target_properties(rime PROPERTIES
  IMPORTED_LOCATION "${T9_LIBRIME_DIR}/bin/rime.dll"
  IMPORTED_IMPLIB "${T9_LIBRIME_DIR}/lib/rime.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${T9_LIBRIME_DIR}/include")

set(T9_RIME_ICE_DIR "${CMAKE_SOURCE_DIR}/data/rime-ice")
if(NOT EXISTS "${T9_RIME_ICE_DIR}/t9.schema.yaml")
  message(FATAL_ERROR "rime-ice not found: run data/fetch_rime_ice.ps1")
endif()

# ---- Pre-deployed Rime data: ${CMAKE_BINARY_DIR}/bin/data ----
set(T9_DATA_DIR "${CMAKE_BINARY_DIR}/bin/data")
add_custom_target(rime_data ALL
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/prepare_data.py"
          --rime-ice "${T9_RIME_ICE_DIR}"
          --custom "${CMAKE_SOURCE_DIR}/data/custom"
          --opencc "${CMAKE_SOURCE_DIR}/third_party/librime/opencc"
          --deployer "${T9_LIBRIME_DIR}/bin/rime_deployer.exe"
          --out "${T9_DATA_DIR}"
  COMMENT "Preparing and pre-deploying Rime data"
  VERBATIM)

enable_testing()

add_custom_target(copy_rime_dll ALL
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/bin"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${T9_LIBRIME_DIR}/bin/rime.dll" "${CMAKE_BINARY_DIR}/bin"
  VERBATIM)

add_subdirectory(src/common)
add_subdirectory(src/tip)
add_subdirectory(src/host/engine)
add_subdirectory(src/host)
add_subdirectory(src/ctl)
add_subdirectory(samples/TestHost)
add_subdirectory(tools/t9repl)
add_subdirectory(tools/t9diag)
add_subdirectory(tests)
