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
  "Optional root containing vendored deps (googletest, fuzztest, abseil-cpp, re2, antlr4). If empty, auto-detects ./OpenMeta-internal/ext, ./openmeta-internal/ext, ./ext, or ../OpenMeta-internal/ext. If unavailable, CMake may use FetchContent.")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT OPENMETA_BUILD_STATIC AND NOT OPENMETA_BUILD_SHARED)
  message(FATAL_ERROR "Enable at least one of OPENMETA_BUILD_STATIC/OPENMETA_BUILD_SHARED.")
endif()

set(OPENMETA_SOURCES
  src/openmeta/byte_arena.cc
  src/openmeta/container_scan.cc
  src/openmeta/meta_edit.cc
  src/openmeta/meta_key.cc
  src/openmeta/meta_store.cc
  src/openmeta/meta_value.cc
)

set(openmeta_deps_root "")
if(OPENMETA_DEPS_ROOT)
  get_filename_component(openmeta_deps_root "${OPENMETA_DEPS_ROOT}" ABSOLUTE
    BASE_DIR "${PROJECT_SOURCE_DIR}")
elseif(EXISTS "${PROJECT_SOURCE_DIR}/openmeta-internal/ext/googletest/CMakeLists.txt")
  set(openmeta_deps_root "${PROJECT_SOURCE_DIR}/openmeta-internal/ext")
elseif(EXISTS "${PROJECT_SOURCE_DIR}/OpenMeta-internal/ext/googletest/CMakeLists.txt")
  set(openmeta_deps_root "${PROJECT_SOURCE_DIR}/OpenMeta-internal/ext")
elseif(EXISTS "${PROJECT_SOURCE_DIR}/ext/googletest/CMakeLists.txt")
  set(openmeta_deps_root "${PROJECT_SOURCE_DIR}/ext")
elseif(EXISTS "${PROJECT_SOURCE_DIR}/../openmeta-internal/ext/googletest/CMakeLists.txt")
  set(openmeta_deps_root "${PROJECT_SOURCE_DIR}/../openmeta-internal/ext")
elseif(EXISTS "${PROJECT_SOURCE_DIR}/../OpenMeta-internal/ext/googletest/CMakeLists.txt")
  set(openmeta_deps_root "${PROJECT_SOURCE_DIR}/../OpenMeta-internal/ext")
endif()

function(openmeta_apply_target_settings target_name)
  target_compile_features(${target_name} PUBLIC cxx_std_20)
  target_include_directories(${target_name} PUBLIC ${PROJECT_SOURCE_DIR}/include)
  set_target_properties(${target_name} PROPERTIES CXX_EXTENSIONS OFF)

  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4)
  else()
    target_compile_options(
      ${target_name}
      PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Werror=return-type
        -fno-exceptions
        -fno-rtti
    )
  endif()
endfunction()

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

  set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

  if(openmeta_deps_root)
    set(FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP "${openmeta_deps_root}/abseil-cpp"
      CACHE PATH "" FORCE)
    set(FETCHCONTENT_SOURCE_DIR_RE2 "${openmeta_deps_root}/re2"
      CACHE PATH "" FORCE)
    set(FETCHCONTENT_SOURCE_DIR_GOOGLETEST "${openmeta_deps_root}/googletest"
      CACHE PATH "" FORCE)
    set(FETCHCONTENT_SOURCE_DIR_ANTLR_CPP
      "${openmeta_deps_root}/antlr4/runtime/Cpp" CACHE PATH "" FORCE)
  endif()

  # The ANTLR4 runtime sets VERSION/SOVERSION on its shared library, which makes
  # CMake create symlinks (not allowed in some sandboxed environments). We only
  # need the static runtime for FuzzTest.
  set(ANTLR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(ANTLR_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)

  set(FUZZTEST_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(FUZZTEST_BUILD_FLATBUFFERS OFF CACHE BOOL "" FORCE)
  set(FUZZTEST_FUZZING_MODE ${OPENMETA_FUZZTEST_FUZZING_MODE} CACHE BOOL ""
    FORCE)
  set(FUZZTEST_COMPATIBILITY_MODE "" CACHE STRING "" FORCE)

  if(openmeta_deps_root AND EXISTS "${openmeta_deps_root}/fuzztest/CMakeLists.txt")
    add_subdirectory("${openmeta_deps_root}/fuzztest"
      "${CMAKE_BINARY_DIR}/_deps/fuzztest-build")
  else()
    if(NOT OPENMETA_FETCH_DEPS)
      message(FATAL_ERROR
        "FuzzTest sources not found (expected `${openmeta_deps_root}/fuzztest`). "
        "Set OPENMETA_DEPS_ROOT (or place deps under ./ext) or enable OPENMETA_FETCH_DEPS=ON.")
    endif()
    include(FetchContent)
    FetchContent_Declare(
      fuzztest
      GIT_REPOSITORY https://github.com/google/fuzztest.git
      GIT_TAG 5a702743bbf29e08f78b4077ccc1d119d4af785b
    )
    FetchContent_MakeAvailable(fuzztest)
  endif()

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
  if(NOT TARGET gtest_main)
    if(openmeta_deps_root AND EXISTS "${openmeta_deps_root}/googletest/CMakeLists.txt")
      add_subdirectory("${openmeta_deps_root}/googletest"
        "${CMAKE_BINARY_DIR}/_deps/googletest-build")
    else()
      if(NOT OPENMETA_FETCH_DEPS)
        message(FATAL_ERROR
          "GoogleTest sources not found (expected `${openmeta_deps_root}/googletest`). "
          "Set OPENMETA_DEPS_ROOT (or place deps under ./ext) or enable OPENMETA_FETCH_DEPS=ON.")
      endif()
      include(FetchContent)
      FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG 6910c9d9165801d8827d628cb72eb7ea9dd538c5
      )
      set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
      FetchContent_MakeAvailable(googletest)
    endif()
  endif()

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
