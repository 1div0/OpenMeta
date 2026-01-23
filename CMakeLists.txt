cmake_minimum_required(VERSION 3.20)

project(OpenMeta VERSION 0.1.0 LANGUAGES CXX)

option(OPENMETA_BUILD_STATIC "Build libopenmeta.a" ON)
option(OPENMETA_BUILD_SHARED "Build libopenmeta.so/.dylib/.dll" ON)
option(OPENMETA_BUILD_TOOLS "Build OpenMeta CLI tools" ON)
option(OPENMETA_BUILD_TESTS "Build OpenMeta unit tests" OFF)
option(OPENMETA_BUILD_FUZZERS "Build OpenMeta libFuzzer targets" OFF)
option(OPENMETA_BUILD_FUZZTEST "Build OpenMeta fuzz tests (FuzzTest)" OFF)
option(OPENMETA_FUZZTEST_FUZZING_MODE "Enable FuzzTest fuzzing mode flags" OFF)
option(OPENMETA_BUILD_DOCS "Build and install API docs (Doxygen)" OFF)

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

set(OPENMETA_SOURCES
  src/openmeta/byte_arena.cc
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
endif()

if(OPENMETA_BUILD_SHARED)
  add_library(openmeta_shared SHARED ${OPENMETA_SOURCES})
  openmeta_apply_target_settings(openmeta_shared)
  set_target_properties(openmeta_shared PROPERTIES OUTPUT_NAME openmeta EXPORT_NAME openmeta_shared)
endif()

if(OPENMETA_BUILD_STATIC)
  set(OPENMETA_LIB_TARGET openmeta_static)
else()
  set(OPENMETA_LIB_TARGET openmeta_shared)
endif()

if(OPENMETA_BUILD_TOOLS)
  add_executable(metaread tools/metaread.cc)
  openmeta_apply_target_settings(metaread)
  target_link_libraries(metaread PRIVATE ${OPENMETA_LIB_TARGET})
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

  add_executable(openmeta_fuzz_exif_tiff_decode
    ${OPENMETA_SOURCES}
    tests/fuzz/exif_tiff_decode_libfuzzer.cc
  )
  openmeta_apply_target_settings(openmeta_fuzz_exif_tiff_decode)

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
  DIRECTORY "${PROJECT_SOURCE_DIR}/include/openmeta"
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
    "${PROJECT_SOURCE_DIR}/include"
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

  install(
    DIRECTORY "${_openmeta_doxygen_out_dir}/html"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
  )
endif()

set(_openmeta_pkg_dir "${CMAKE_INSTALL_LIBDIR}/cmake/OpenMeta")

install(
  EXPORT OpenMetaTargets
  NAMESPACE OpenMeta::
  FILE OpenMetaTargets.cmake
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

openmeta_print_config_summary()
