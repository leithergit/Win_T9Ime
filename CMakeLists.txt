cmake_minimum_required(VERSION 3.25)
project(T9Ime VERSION 0.6.0 LANGUAGES CXX RC)

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

# ---- Version: project(VERSION) above, build number = git commit count ----
set(T9_BUILD_NUMBER 0)
set(T9_COMMIT "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                  OUTPUT_VARIABLE T9_BUILD_NUMBER OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                  OUTPUT_VARIABLE T9_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  # Configure again after every commit, so the numbers stay current.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/.git/HEAD")
  if(EXISTS "${CMAKE_SOURCE_DIR}/.git/logs/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/.git/logs/HEAD")
  endif()
endif()
if(NOT T9_BUILD_NUMBER MATCHES "^[0-9]+$")
  set(T9_BUILD_NUMBER 0)
endif()
message(STATUS "T9Ime ${PROJECT_VERSION}.${T9_BUILD_NUMBER} (${T9_COMMIT})")

# Windows version resource (file properties -> Details) for `target`.
function(t9_version_resource target description)
  get_target_property(type ${target} TYPE)
  get_target_property(output ${target} OUTPUT_NAME)
  if(NOT output)
    set(output ${target})
  endif()
  if(type STREQUAL "SHARED_LIBRARY")
    set(T9_FILE_TYPE "VFT_DLL")
    set(T9_ORIGINAL_FILENAME "${output}.dll")
  else()
    set(T9_FILE_TYPE "VFT_APP")
    set(T9_ORIGINAL_FILENAME "${output}.exe")
  endif()
  set(T9_FILE_DESCRIPTION "${description}")
  set(T9_INTERNAL_NAME "${output}")
  configure_file("${CMAKE_SOURCE_DIR}/cmake/version.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc" @ONLY)
  target_sources(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
endfunction()

# The T9Ime icon (res/T9Ime.ico, drawn by tools/make_icon.py) as resource 1:
# the first icon of the file, which the TSF profile registration points to.
function(t9_icon_resource target)
  file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/res/T9Ime.ico" ico)
  string(REPLACE "\\" "\\\\" ico "${ico}")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${target}_icon.rc" "1 ICON \"${ico}\"\n")
  target_sources(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${target}_icon.rc")
  set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/${target}_icon.rc" APPEND PROPERTY OBJECT_DEPENDS
               "${CMAKE_SOURCE_DIR}/res/T9Ime.ico")
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
