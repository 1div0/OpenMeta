cmake_minimum_required(VERSION 3.20)

project(OpenMeta VERSION 0.3.0 LANGUAGES CXX)

option(OPENMETA_BUILD_STATIC "Build libopenmeta.a" ON)
option(OPENMETA_BUILD_SHARED "Build libopenmeta.so/.dylib/.dll" ON)
option(OPENMETA_BUILD_TOOLS "Build OpenMeta CLI tools" ON)
option(OPENMETA_BUILD_TESTS "Build OpenMeta unit tests" OFF)
option(OPENMETA_BUILD_FUZZERS "Build OpenMeta libFuzzer targets" OFF)
option(OPENMETA_BUILD_FUZZTEST "Build OpenMeta fuzz tests (FuzzTest)" OFF)
option(OPENMETA_FUZZTEST_FUZZING_MODE "Enable FuzzTest fuzzing mode flags" OFF)
option(OPENMETA_BUILD_DOCS "Build and install API docs (Doxygen)" OFF)
option(OPENMETA_BUILD_PYTHON "Build Python bindings (nanobind)" OFF)
option(OPENMETA_BUILD_WHEEL "Add an `openmeta_wheel` target (Python wheel build)" OFF)
option(OPENMETA_WHEEL_NO_BUILD_ISOLATION "Build wheel without isolation (requires scikit-build-core installed)" OFF)
option(OPENMETA_WITH_ZLIB "Enable zlib/deflate payload decompression" ON)
option(OPENMETA_WITH_BROTLI "Enable brotli payload decompression" ON)

include(GNUInstallDirs)

# Optional path to local dependency repos (used for the FuzzTest wrapper).
# Expected layout:
#   <root>/fuzztest
#   <root>/re2
#   <root>/antlr4
set(OPENMETA_DEPS_REPOS_ROOT "" CACHE PATH "Path to local dependency repos for optional source builds")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT OPENMETA_BUILD_STATIC AND NOT OPENMETA_BUILD_SHARED)
  message(FATAL_ERROR "Enable at least one of OPENMETA_BUILD_STATIC/OPENMETA_BUILD_SHARED.")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(OpenMetaDependencies)
include(OpenMetaTargetSettings)
include(OpenMetaReport)

if(OPENMETA_BUILD_DOCS)
  find_package(Doxygen REQUIRED)
endif()

set(OPENMETA_ZLIB_FOUND OFF)
if(OPENMETA_WITH_ZLIB)
  find_package(ZLIB QUIET)
  if(ZLIB_FOUND)
    set(OPENMETA_ZLIB_FOUND ON)
  endif()
endif()

set(OPENMETA_BROTLI_FOUND OFF)
if(OPENMETA_WITH_BROTLI)
  find_package(Brotli QUIET)
  if(Brotli_FOUND)
    set(OPENMETA_BROTLI_FOUND ON)
  endif()
endif()

set(OPENMETA_CONFIG_HAS_ZLIB 0)
if(OPENMETA_ZLIB_FOUND)
  set(OPENMETA_CONFIG_HAS_ZLIB 1)
endif()

set(OPENMETA_CONFIG_HAS_BROTLI 0)
if(OPENMETA_BROTLI_FOUND)
  set(OPENMETA_CONFIG_HAS_BROTLI 1)
endif()

set(OPENMETA_SOURCES
  src/openmeta/byte_arena.cc
  src/openmeta/container_payload.cc
  src/openmeta/container_scan.cc
  src/openmeta/exif_tag_names.cc
  src/openmeta/exif_tiff_decode.cc
  src/openmeta/meta_edit.cc
  src/openmeta/meta_key.cc
  src/openmeta/meta_store.cc
  src/openmeta/meta_value.cc
  src/openmeta/simple_meta.cc
)

if(OPENMETA_BUILD_STATIC)
  add_library(openmeta_static STATIC ${OPENMETA_SOURCES})
  openmeta_apply_target_settings(openmeta_static)
  set_target_properties(openmeta_static PROPERTIES OUTPUT_NAME openmeta EXPORT_NAME openmeta_static)
  set_target_properties(openmeta_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(OPENMETA_BUILD_SHARED)
  add_library(openmeta_shared SHARED ${OPENMETA_SOURCES})
  openmeta_apply_target_settings(openmeta_shared)
  set_target_properties(openmeta_shared PROPERTIES OUTPUT_NAME openmeta EXPORT_NAME openmeta_shared)
endif()

foreach(_tgt IN ITEMS openmeta_static openmeta_shared)
  if(TARGET ${_tgt})
    if(OPENMETA_ZLIB_FOUND)
      target_link_libraries(${_tgt} PUBLIC ZLIB::ZLIB)
      target_compile_definitions(${_tgt} PUBLIC OPENMETA_HAS_ZLIB=1)
    endif()
    if(OPENMETA_BROTLI_FOUND)
      target_link_libraries(${_tgt} PUBLIC Brotli::decoder)
      target_compile_definitions(${_tgt} PUBLIC OPENMETA_HAS_BROTLI=1)
    endif()
  endif()
endforeach()

if(OPENMETA_BUILD_STATIC)
  set(OPENMETA_LIB_TARGET openmeta_static)
else()
  set(OPENMETA_LIB_TARGET openmeta_shared)
endif()

if(OPENMETA_BUILD_TOOLS)
  add_executable(metaread src/tools/metaread.cc)
  openmeta_apply_target_settings(metaread)
  target_link_libraries(metaread PRIVATE ${OPENMETA_LIB_TARGET})
endif()

if(OPENMETA_BUILD_PYTHON)
  add_subdirectory(src/python)
endif()

if(OPENMETA_BUILD_WHEEL)
  find_package(Python REQUIRED COMPONENTS Interpreter)
  find_program(OPENMETA_UV_EXECUTABLE NAMES uv)
  execute_process(
    COMMAND "${Python_EXECUTABLE}" -c "import pip"
    RESULT_VARIABLE _openmeta_python_has_pip
    OUTPUT_QUIET ERROR_QUIET
  )

  set(_openmeta_wheel_env_prefix_path "${CMAKE_PREFIX_PATH}")
  string(REPLACE ";" "\\;" _openmeta_wheel_env_prefix_path "${_openmeta_wheel_env_prefix_path}")
  set(_openmeta_wheel_out_dir "${CMAKE_BINARY_DIR}/dist")

  set(_openmeta_wheel_pip_isolation_args "")
  if(OPENMETA_WHEEL_NO_BUILD_ISOLATION)
    set(_openmeta_wheel_pip_isolation_args --no-build-isolation)
  endif()

  set(_openmeta_wheel_cmd "")
  set(_openmeta_wheel_tool "pip")
  if(_openmeta_python_has_pip EQUAL 0)
    list(APPEND _openmeta_wheel_cmd
      "${Python_EXECUTABLE}" -m pip wheel "${PROJECT_SOURCE_DIR}"
      --no-deps
      ${_openmeta_wheel_pip_isolation_args}
      -w "${_openmeta_wheel_out_dir}"
    )
  else()
    if(NOT OPENMETA_UV_EXECUTABLE)
      message(FATAL_ERROR
        "OPENMETA_BUILD_WHEEL: '${Python_EXECUTABLE}' has no pip module, and 'uv' was not found on PATH. "
        "Either install pip into that Python, or install uv."
      )
    endif()
    set(_openmeta_wheel_tool "uv")
    set(_openmeta_wheel_uv_args "")
    if(OPENMETA_WHEEL_NO_BUILD_ISOLATION)
      set(_openmeta_wheel_uv_args --no-build-isolation)
    endif()
    list(APPEND _openmeta_wheel_cmd
      "${OPENMETA_UV_EXECUTABLE}" --no-cache build
      --wheel
      ${_openmeta_wheel_uv_args}
      -p "${Python_EXECUTABLE}"
      -o "${_openmeta_wheel_out_dir}"
      "${PROJECT_SOURCE_DIR}"
    )
  endif()

  add_custom_target(openmeta_wheel
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_openmeta_wheel_out_dir}"
    COMMAND "${CMAKE_COMMAND}" -E env
            "CMAKE_PREFIX_PATH=${_openmeta_wheel_env_prefix_path}"
            "CC=${CMAKE_C_COMPILER}"
            "CXX=${CMAKE_CXX_COMPILER}"
            "SKBUILD_CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            ${_openmeta_wheel_cmd}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Building OpenMeta Python wheel (${_openmeta_wheel_tool})"
    VERBATIM
  )

  set(_openmeta_install_wheel_script "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaInstallWheel.cmake")
  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/OpenMetaInstallWheel.cmake.in"
    "${_openmeta_install_wheel_script}"
    @ONLY
  )
endif()

if(OPENMETA_BUILD_FUZZTEST)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "OPENMETA_BUILD_FUZZTEST requires Clang.")
  endif()

  openmeta_add_fuzztest()

  add_executable(openmeta_fuzztest_metastore
    ${OPENMETA_SOURCES}
    tests/fuzztest/metastore_fuzztest.cc
  )
  openmeta_apply_target_settings(openmeta_fuzztest_metastore)
  openmeta_apply_core_deps(openmeta_fuzztest_metastore)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(openmeta_fuzztest_metastore PRIVATE -Wno-nullability-extension)
  endif()
  target_link_libraries(openmeta_fuzztest_metastore PRIVATE fuzztest::fuzztest)
  target_link_libraries(openmeta_fuzztest_metastore PRIVATE fuzztest::fuzztest_gtest_main)

  if(OPENMETA_FUZZTEST_FUZZING_MODE)
    # Abseil enables "swisstable generations" automatically in sanitizer builds,
    # which changes internal container layouts. When linking against prebuilt
    # Abseil/FuzzTest without sanitizers, this can cause ODR/ABI mismatches and
    # early process aborts. Disable the generations feature for this target so
    # sanitized OpenMeta code can link against non-sanitized dependencies.
    target_compile_definitions(openmeta_fuzztest_metastore PRIVATE NDEBUG_SANITIZER)
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
  openmeta_add_googletest()

  add_executable(openmeta_tests
    tests/container_payload_test.cc
    tests/container_scan_test.cc
    tests/exif_tag_names_test.cc
    tests/exif_tiff_decode_test.cc
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
  openmeta_apply_core_deps(openmeta_fuzz_metastore)

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
  openmeta_apply_core_deps(openmeta_fuzz_container_scan)

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

  add_executable(openmeta_fuzz_bmff_meta
    ${OPENMETA_SOURCES}
    tests/fuzz/bmff_meta_libfuzzer.cc
  )
  openmeta_apply_target_settings(openmeta_fuzz_bmff_meta)
  openmeta_apply_core_deps(openmeta_fuzz_bmff_meta)

  target_compile_options(
    openmeta_fuzz_bmff_meta
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(
    openmeta_fuzz_bmff_meta
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )

  add_executable(openmeta_fuzz_exif_tiff_decode
    ${OPENMETA_SOURCES}
    tests/fuzz/exif_tiff_decode_libfuzzer.cc
  )
  openmeta_apply_target_settings(openmeta_fuzz_exif_tiff_decode)
  openmeta_apply_core_deps(openmeta_fuzz_exif_tiff_decode)

  target_compile_options(
    openmeta_fuzz_exif_tiff_decode
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(
    openmeta_fuzz_exif_tiff_decode
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )

  add_executable(openmeta_fuzz_container_payload
    ${OPENMETA_SOURCES}
    tests/fuzz/container_payload_libfuzzer.cc
  )
  openmeta_apply_target_settings(openmeta_fuzz_container_payload)
  openmeta_apply_core_deps(openmeta_fuzz_container_payload)

  target_compile_options(
    openmeta_fuzz_container_payload
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(
    openmeta_fuzz_container_payload
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
endif()

include(CMakePackageConfigHelpers)

set(_openmeta_install_targets "")
if(TARGET openmeta_static)
  list(APPEND _openmeta_install_targets openmeta_static)
endif()
if(TARGET openmeta_shared)
  list(APPEND _openmeta_install_targets openmeta_shared)
endif()

install(
  TARGETS ${_openmeta_install_targets}
  EXPORT OpenMetaTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/src/include/openmeta"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

if(TARGET metaread)
  install(TARGETS metaread RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(OPENMETA_BUILD_DOCS)
  set(_openmeta_doxygen_out_dir "${CMAKE_CURRENT_BINARY_DIR}/docs")
  set(_openmeta_doxygen_cfg "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaDoxyfile")
  set(_openmeta_install_docs_script "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaInstallDocs.cmake")

  set(_openmeta_doxygen_inputs
    "${PROJECT_SOURCE_DIR}/README.md"
    "${PROJECT_SOURCE_DIR}/src/include"
    "${PROJECT_SOURCE_DIR}/docs"
  )
  string(JOIN " " OPENMETA_DOXYGEN_INPUT ${_openmeta_doxygen_inputs})
  set(OPENMETA_DOXYGEN_OUTPUT_DIR "${_openmeta_doxygen_out_dir}")
  set(OPENMETA_DOXYGEN_MAINPAGE "${PROJECT_SOURCE_DIR}/README.md")

  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/Doxyfile.in"
    "${_openmeta_doxygen_cfg}"
    @ONLY
  )

  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/OpenMetaInstallDocs.cmake.in"
    "${_openmeta_install_docs_script}"
    @ONLY
  )

  add_custom_target(openmeta_docs
    COMMAND "${DOXYGEN_EXECUTABLE}" "${_openmeta_doxygen_cfg}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Generating OpenMeta API documentation (Doxygen)"
    VERBATIM
  )

  install(SCRIPT "${_openmeta_install_docs_script}")
endif()

set(_openmeta_pkg_dir "${CMAKE_INSTALL_LIBDIR}/cmake/OpenMeta")

install(
  EXPORT OpenMetaTargets
  NAMESPACE OpenMeta::
  FILE OpenMetaTargets.cmake
  DESTINATION "${_openmeta_pkg_dir}"
)

install(
  FILES
    "${PROJECT_SOURCE_DIR}/cmake/FindBrotli.cmake"
  DESTINATION "${_openmeta_pkg_dir}"
)

configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/OpenMetaConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaConfig.cmake"
  INSTALL_DESTINATION "${_openmeta_pkg_dir}"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaConfigVersion.cmake"
  DESTINATION "${_openmeta_pkg_dir}"
)

if(OPENMETA_BUILD_WHEEL)
  install(SCRIPT "${_openmeta_install_wheel_script}")
endif()

openmeta_print_config_summary()
