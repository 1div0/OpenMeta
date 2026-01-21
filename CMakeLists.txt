cmake_minimum_required(VERSION 3.20)

project(OpenMeta VERSION 0.1.0 LANGUAGES CXX)

option(OPENMETA_BUILD_STATIC "Build libopenmeta.a" ON)
option(OPENMETA_BUILD_SHARED "Build libopenmeta.so/.dylib/.dll" ON)
option(OPENMETA_BUILD_TESTS "Build OpenMeta unit tests" OFF)
option(OPENMETA_BUILD_FUZZERS "Build OpenMeta libFuzzer targets" OFF)
option(OPENMETA_BUILD_FUZZTEST "Build OpenMeta fuzz tests (FuzzTest)" OFF)
option(OPENMETA_FUZZTEST_FUZZING_MODE "Enable FuzzTest fuzzing mode flags" OFF)
option(OPENMETA_FETCH_DEPS "Allow FetchContent to download missing deps" OFF)

set(OPENMETA_DEPS_ROOT "" CACHE PATH
  "Optional root containing vendored deps (googletest, fuzztest, abseil-cpp, re2, antlr4). If empty, auto-detects ../OpenMeta-internal/ext (and similar fallbacks). If unavailable, CMake may use FetchContent when OPENMETA_FETCH_DEPS=ON.")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT OPENMETA_BUILD_STATIC AND NOT OPENMETA_BUILD_SHARED)
  message(FATAL_ERROR "Enable at least one of OPENMETA_BUILD_STATIC/OPENMETA_BUILD_SHARED.")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(OpenMetaDependencies)
include(OpenMetaTargetSettings)

set(OPENMETA_SOURCES
  src/openmeta/byte_arena.cc
  src/openmeta/container_scan.cc
  src/openmeta/meta_edit.cc
  src/openmeta/meta_key.cc
  src/openmeta/meta_store.cc
  src/openmeta/meta_value.cc
)

openmeta_resolve_deps_root(openmeta_deps_root)

if(OPENMETA_BUILD_STATIC)
  add_library(openmeta_static STATIC ${OPENMETA_SOURCES})
  openmeta_apply_target_settings(openmeta_static)
  set_target_properties(openmeta_static PROPERTIES OUTPUT_NAME openmeta)
endif()

if(OPENMETA_BUILD_SHARED)
  add_library(openmeta_shared SHARED ${OPENMETA_SOURCES})
  openmeta_apply_target_settings(openmeta_shared)
  set_target_properties(openmeta_shared PROPERTIES OUTPUT_NAME openmeta)
endif()

if(OPENMETA_BUILD_STATIC)
  set(OPENMETA_LIB_TARGET openmeta_static)
else()
  set(OPENMETA_LIB_TARGET openmeta_shared)
endif()

if(OPENMETA_BUILD_FUZZTEST)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "OPENMETA_BUILD_FUZZTEST requires Clang.")
  endif()

  openmeta_add_fuzztest("${openmeta_deps_root}")

  add_executable(openmeta_fuzztest_metastore
    ${OPENMETA_SOURCES}
    tests/fuzztest/metastore_fuzztest.cc
  )
  openmeta_apply_target_settings(openmeta_fuzztest_metastore)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(openmeta_fuzztest_metastore PRIVATE -Wno-nullability-extension)
  endif()
  target_link_libraries(openmeta_fuzztest_metastore PRIVATE fuzztest::fuzztest)
  target_link_libraries(openmeta_fuzztest_metastore PRIVATE fuzztest::fuzztest_gtest_main)

  if(OPENMETA_FUZZTEST_FUZZING_MODE)
    target_compile_options(
      openmeta_fuzztest_metastore
      PRIVATE
        -g
        -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
        -UNDEBUG
        -fsanitize-coverage=inline-8bit-counters
        -fsanitize-coverage=trace-cmp
        -fsanitize=address
        -DADDRESS_SANITIZER
        -fno-omit-frame-pointer
    )
    target_link_options(
      openmeta_fuzztest_metastore
      PRIVATE
        -fsanitize=address
        -fno-omit-frame-pointer
    )
  endif()
endif()

if(OPENMETA_BUILD_TESTS)
  enable_testing()
  openmeta_add_googletest("${openmeta_deps_root}")

  add_executable(openmeta_tests
    tests/container_scan_test.cc
    tests/meta_store_test.cc
  )
  target_link_libraries(openmeta_tests PRIVATE ${OPENMETA_LIB_TARGET} gtest_main)

  add_test(NAME openmeta_tests COMMAND openmeta_tests)
endif()

if(OPENMETA_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "OPENMETA_BUILD_FUZZERS requires Clang (libFuzzer).")
  endif()

  add_executable(openmeta_fuzz_metastore
    ${OPENMETA_SOURCES}
    tests/fuzz/metastore_libfuzzer.cc
  )
  openmeta_apply_target_settings(openmeta_fuzz_metastore)

  target_compile_options(
    openmeta_fuzz_metastore
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(
    openmeta_fuzz_metastore
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )

  add_executable(openmeta_fuzz_container_scan
    ${OPENMETA_SOURCES}
    tests/fuzz/container_scan_libfuzzer.cc
  )
  openmeta_apply_target_settings(openmeta_fuzz_container_scan)

  target_compile_options(
    openmeta_fuzz_container_scan
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(
    openmeta_fuzz_container_scan
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
endif()
