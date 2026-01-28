cmake_minimum_required(VERSION 3.20)

# Single source of truth for the project version (also used by the Python wheel).
set(_openmeta_version_file "${CMAKE_CURRENT_LIST_DIR}/VERSION")
if(NOT EXISTS "${_openmeta_version_file}")
  message(FATAL_ERROR "Missing version file: ${_openmeta_version_file}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_openmeta_version_file}")
file(READ "${_openmeta_version_file}" OPENMETA_VERSION)
string(STRIP "${OPENMETA_VERSION}" OPENMETA_VERSION)
if(NOT OPENMETA_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR
    "Invalid OpenMeta version in ${_openmeta_version_file}: '${OPENMETA_VERSION}' (expected MAJOR.MINOR.PATCH)")
endif()
set(OPENMETA_VERSION "${OPENMETA_VERSION}" CACHE STRING "OpenMeta project version" FORCE)

# Preserve CMAKE_PREFIX_PATH in the cache so GUI tools don't lose it between runs.
if(NOT CMAKE_PREFIX_PATH AND DEFINED ENV{CMAKE_PREFIX_PATH} AND NOT "$ENV{CMAKE_PREFIX_PATH}" STREQUAL "")
  set(_openmeta_env_prefix_path "$ENV{CMAKE_PREFIX_PATH}")
  if(NOT _openmeta_env_prefix_path MATCHES ";")
    string(REPLACE ":" ";" _openmeta_env_prefix_path "${_openmeta_env_prefix_path}")
  endif()
  set(CMAKE_PREFIX_PATH "${_openmeta_env_prefix_path}" CACHE PATH "CMake prefix search path" FORCE)
endif()

project(OpenMeta VERSION "${OPENMETA_VERSION}" LANGUAGES CXX)

option(OPENMETA_BUILD_STATIC "Build libopenmeta.a" ON)
option(OPENMETA_BUILD_SHARED "Build libopenmeta.so/.dylib/.dll" ON)
option(OPENMETA_BUILD_TOOLS "Build OpenMeta CLI tools" ON)
option(OPENMETA_BUILD_TESTS "Build OpenMeta unit tests" OFF)
option(OPENMETA_BUILD_FUZZERS "Build OpenMeta libFuzzer targets" OFF)
option(OPENMETA_BUILD_FUZZTEST "Build OpenMeta fuzz tests (FuzzTest)" OFF)
option(OPENMETA_FUZZTEST_FUZZING_MODE "Enable FuzzTest fuzzing mode flags" OFF)
option(OPENMETA_BUILD_DOCS "Build and install API docs (Doxygen)" OFF)
option(OPENMETA_BUILD_SPHINX_DOCS "Build and install HTML docs via Sphinx (Doxygen XML + Breathe)" OFF)
option(OPENMETA_BUILD_PYTHON "Build Python bindings (nanobind)" OFF)
option(OPENMETA_BUILD_WHEEL "Add an `openmeta_wheel` target (Python wheel build)" OFF)
option(OPENMETA_WHEEL_NO_BUILD_ISOLATION "Build wheel without isolation (requires scikit-build-core installed)" OFF)
option(OPENMETA_WITH_ZLIB "Enable zlib/deflate payload decompression" ON)
option(OPENMETA_WITH_BROTLI "Enable brotli payload decompression" ON)
option(OPENMETA_WITH_EXPAT "Enable XMP packet decode via Expat" ON)
option(OPENMETA_USE_LIBCXX "Use libc++ as the C++ standard library (Clang)" OFF)

include(GNUInstallDirs)

set(OPENMETA_BUILD_TIMESTAMP "" CACHE STRING "Build timestamp in UTC (ISO-8601). Leave empty to set on first configure")

# Python interpreter override.
#
# CMake caches the discovered interpreter in the build directory, and CMake GUI
# often runs outside of an activated environment (uv/venv/conda). To ensure docs
# and wheel builds use the intended Python, provide an explicit interpreter here.
set(OPENMETA_PYTHON_EXECUTABLE "" CACHE FILEPATH "Override Python interpreter used by OpenMeta (docs/wheel/bindings)")
if(OPENMETA_PYTHON_EXECUTABLE)
  if(NOT EXISTS "${OPENMETA_PYTHON_EXECUTABLE}")
    message(FATAL_ERROR "OPENMETA_PYTHON_EXECUTABLE does not exist: ${OPENMETA_PYTHON_EXECUTABLE}")
  endif()
  set(Python_EXECUTABLE "${OPENMETA_PYTHON_EXECUTABLE}" CACHE FILEPATH "Python interpreter" FORCE)
endif()

# Optional path to local dependency repos (used for the FuzzTest wrapper).
# Expected layout:
#   <root>/fuzztest
#   <root>/re2
#   <root>/antlr4
set(OPENMETA_DEPS_REPOS_ROOT "" CACHE PATH "Path to local dependency repos for optional source builds")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(OPENMETA_USE_LIBCXX)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "OPENMETA_USE_LIBCXX requires Clang.")
  endif()
  add_compile_options(-stdlib=libc++)
  add_link_options(-stdlib=libc++)
endif()

if(NOT OPENMETA_BUILD_STATIC AND NOT OPENMETA_BUILD_SHARED)
  message(FATAL_ERROR "Enable at least one of OPENMETA_BUILD_STATIC/OPENMETA_BUILD_SHARED.")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(OpenMetaDependencies)
include(OpenMetaTargetSettings)
include(OpenMetaReport)

if(OPENMETA_BUILD_DOCS OR OPENMETA_BUILD_SPHINX_DOCS)
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

set(OPENMETA_EXPAT_FOUND OFF)
if(OPENMETA_WITH_EXPAT)
  find_package(EXPAT QUIET)
  if(EXPAT_FOUND)
    set(OPENMETA_EXPAT_FOUND ON)
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

set(OPENMETA_CONFIG_HAS_EXPAT 0)
if(OPENMETA_EXPAT_FOUND)
  set(OPENMETA_CONFIG_HAS_EXPAT 1)
endif()

if(OPENMETA_BUILD_TIMESTAMP STREQUAL "")
  string(TIMESTAMP _openmeta_ts "%Y-%m-%dT%H:%M:%SZ" UTC)
  set(OPENMETA_BUILD_TIMESTAMP "${_openmeta_ts}" CACHE STRING "Build timestamp in UTC (ISO-8601). Leave empty to set on first configure" FORCE)
endif()

set(OPENMETA_BUILDINFO_BUILD_TYPE "unknown")
if(CMAKE_CONFIGURATION_TYPES)
  set(OPENMETA_BUILDINFO_BUILD_TYPE "multi-config")
elseif(CMAKE_BUILD_TYPE)
  set(OPENMETA_BUILDINFO_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
endif()

set(OPENMETA_BUILDINFO_TIMESTAMP_UTC "${OPENMETA_BUILD_TIMESTAMP}")
set(OPENMETA_BUILDINFO_WITH_ZLIB "${OPENMETA_WITH_ZLIB}")
set(OPENMETA_BUILDINFO_WITH_BROTLI "${OPENMETA_WITH_BROTLI}")
set(OPENMETA_BUILDINFO_WITH_EXPAT "${OPENMETA_WITH_EXPAT}")

set(_openmeta_generated_include_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
file(MAKE_DIRECTORY "${_openmeta_generated_include_dir}/openmeta")
configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/OpenMetaBuildInfoGenerated.h.in"
  "${_openmeta_generated_include_dir}/openmeta/build_info_generated.h"
  @ONLY
)

set(OPENMETA_SOURCES
  src/openmeta/build_info.cc
  src/openmeta/console_format.cc
  src/openmeta/byte_arena.cc
  src/openmeta/container_payload.cc
  src/openmeta/container_scan.cc
  src/openmeta/icc_decode.cc
  src/openmeta/exif_tag_names.cc
  src/openmeta/exif_tiff_decode.cc
  src/openmeta/iptc_iim_decode.cc
  src/openmeta/xmp_decode.cc
  src/openmeta/meta_edit.cc
  src/openmeta/meta_key.cc
  src/openmeta/meta_store.cc
  src/openmeta/meta_value.cc
  src/openmeta/photoshop_irb_decode.cc
  src/openmeta/simple_meta.cc
)

if(OPENMETA_BUILD_STATIC)
  add_library(openmeta_static STATIC ${OPENMETA_SOURCES})
  openmeta_apply_target_settings(openmeta_static)
  set_target_properties(openmeta_static PROPERTIES OUTPUT_NAME openmeta EXPORT_NAME openmeta_static)
  set_target_properties(openmeta_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(openmeta_static PRIVATE OPENMETA_BUILD_LINKAGE_STATIC=1)
  target_include_directories(openmeta_static PRIVATE "${_openmeta_generated_include_dir}")
endif()

if(OPENMETA_BUILD_SHARED)
  add_library(openmeta_shared SHARED ${OPENMETA_SOURCES})
  openmeta_apply_target_settings(openmeta_shared)
  set_target_properties(openmeta_shared PROPERTIES OUTPUT_NAME openmeta EXPORT_NAME openmeta_shared)
  target_compile_definitions(openmeta_shared PRIVATE OPENMETA_BUILD_LINKAGE_SHARED=1)
  target_include_directories(openmeta_shared PRIVATE "${_openmeta_generated_include_dir}")
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
    if(OPENMETA_EXPAT_FOUND)
      if(TARGET EXPAT::EXPAT)
        target_link_libraries(${_tgt} PUBLIC EXPAT::EXPAT)
      else()
        target_include_directories(${_tgt} PUBLIC "${EXPAT_INCLUDE_DIRS}")
        target_link_libraries(${_tgt} PUBLIC "${EXPAT_LIBRARIES}")
      endif()
      target_compile_definitions(${_tgt} PUBLIC OPENMETA_HAS_EXPAT=1)
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

  if(OPENMETA_WHEEL_NO_BUILD_ISOLATION)
    execute_process(
      COMMAND "${Python_EXECUTABLE}" -c "import scikit_build_core"
      RESULT_VARIABLE _openmeta_python_has_scikit_build_core
      OUTPUT_QUIET ERROR_QUIET
    )
    if(NOT _openmeta_python_has_scikit_build_core EQUAL 0)
      message(FATAL_ERROR
        "OPENMETA_WHEEL_NO_BUILD_ISOLATION requires Python package 'scikit-build-core' (module 'scikit_build_core') "
        "for: ${Python_EXECUTABLE}. Install it into that interpreter (e.g. pip install scikit-build-core), "
        "or set OPENMETA_WHEEL_NO_BUILD_ISOLATION=OFF.")
    endif()
  endif()

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
    tests/icc_decode_test.cc
    tests/exif_tag_names_test.cc
    tests/exif_tiff_decode_test.cc
    tests/iptc_iim_decode_test.cc
    tests/meta_store_test.cc
    tests/photoshop_irb_decode_test.cc
    tests/xmp_decode_test.cc
  )
  target_link_libraries(openmeta_tests PRIVATE ${OPENMETA_LIB_TARGET} gtest_main)

  add_test(NAME openmeta_tests COMMAND openmeta_tests)
endif()

if(OPENMETA_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "OPENMETA_BUILD_FUZZERS requires Clang (libFuzzer).")
  endif()

  function(openmeta_apply_libfuzzer_stdlib target_name)
    # On Linux, Clang's bundled libFuzzer runtime is typically built against
    # libstdc++. If the rest of the build uses libc++ (e.g. to match prebuilt
    # GoogleTest), override the fuzz targets to use libstdc++ so they can link.
    if(OPENMETA_USE_LIBCXX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_compile_options(${target_name} PRIVATE -stdlib=libstdc++)
      target_link_options(${target_name} PRIVATE -stdlib=libstdc++)
    endif()
  endfunction()

  add_executable(openmeta_fuzz_metastore
    ${OPENMETA_SOURCES}
    tests/fuzz/metastore_libfuzzer.cc
  )
  openmeta_apply_target_settings(openmeta_fuzz_metastore)
  openmeta_apply_core_deps(openmeta_fuzz_metastore)
  openmeta_apply_libfuzzer_stdlib(openmeta_fuzz_metastore)

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
  openmeta_apply_libfuzzer_stdlib(openmeta_fuzz_container_scan)

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
  openmeta_apply_libfuzzer_stdlib(openmeta_fuzz_bmff_meta)

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
  openmeta_apply_libfuzzer_stdlib(openmeta_fuzz_exif_tiff_decode)

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

  if(OPENMETA_EXPAT_FOUND)
    add_executable(openmeta_fuzz_xmp_decode
      ${OPENMETA_SOURCES}
      tests/fuzz/xmp_decode_libfuzzer.cc
    )
    openmeta_apply_target_settings(openmeta_fuzz_xmp_decode)
    openmeta_apply_core_deps(openmeta_fuzz_xmp_decode)
    openmeta_apply_libfuzzer_stdlib(openmeta_fuzz_xmp_decode)

    target_compile_options(
      openmeta_fuzz_xmp_decode
      PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(
      openmeta_fuzz_xmp_decode
      PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
  endif()

  add_executable(openmeta_fuzz_container_payload
    ${OPENMETA_SOURCES}
    tests/fuzz/container_payload_libfuzzer.cc
  )
  openmeta_apply_target_settings(openmeta_fuzz_container_payload)
  openmeta_apply_core_deps(openmeta_fuzz_container_payload)
  openmeta_apply_libfuzzer_stdlib(openmeta_fuzz_container_payload)

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

if(OPENMETA_BUILD_DOCS OR OPENMETA_BUILD_SPHINX_DOCS)
  set(_openmeta_docs_root_dir "${CMAKE_CURRENT_BINARY_DIR}/docs")
  set(_openmeta_doxygen_out_dir "${_openmeta_docs_root_dir}/doxygen")
  set(_openmeta_doxygen_cfg "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaDoxyfile")

  set(_openmeta_doxygen_inputs
    "${PROJECT_SOURCE_DIR}/README.md"
    "${PROJECT_SOURCE_DIR}/src/include"
    "${PROJECT_SOURCE_DIR}/docs"
  )
  string(JOIN " " OPENMETA_DOXYGEN_INPUT ${_openmeta_doxygen_inputs})
  set(OPENMETA_DOXYGEN_OUTPUT_DIR "${_openmeta_doxygen_out_dir}")
  set(OPENMETA_DOXYGEN_MAINPAGE "${PROJECT_SOURCE_DIR}/README.md")

  set(OPENMETA_DOXYGEN_GENERATE_XML "NO")
  if(OPENMETA_BUILD_SPHINX_DOCS)
    set(OPENMETA_DOXYGEN_GENERATE_XML "YES")
  endif()

  set(_openmeta_doxygen_xml_dir "${_openmeta_doxygen_out_dir}/xml")
  file(TO_CMAKE_PATH "${_openmeta_doxygen_xml_dir}" OPENMETA_DOXYGEN_XML_DIR)

  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/Doxyfile.in"
    "${_openmeta_doxygen_cfg}"
    @ONLY
  )

  add_custom_target(openmeta_docs
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_openmeta_doxygen_out_dir}"
    COMMAND "${DOXYGEN_EXECUTABLE}" "${_openmeta_doxygen_cfg}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Generating OpenMeta API documentation (Doxygen)"
    VERBATIM
  )

  if(OPENMETA_BUILD_DOCS)
    set(_openmeta_install_docs_script "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaInstallDocs.cmake")
    set(OPENMETA_DOXYGEN_INSTALL_SUBDIR "")
    if(OPENMETA_BUILD_SPHINX_DOCS)
      set(OPENMETA_DOXYGEN_INSTALL_SUBDIR "doxygen")
    endif()
    configure_file(
      "${PROJECT_SOURCE_DIR}/cmake/OpenMetaInstallDocs.cmake.in"
      "${_openmeta_install_docs_script}"
      @ONLY
    )
    install(SCRIPT "${_openmeta_install_docs_script}")
  endif()

  if(OPENMETA_BUILD_SPHINX_DOCS)
    find_package(Python REQUIRED COMPONENTS Interpreter)

    execute_process(
      COMMAND "${Python_EXECUTABLE}" -c "import sphinx, breathe"
      RESULT_VARIABLE _openmeta_sphinx_import_res
      OUTPUT_QUIET ERROR_QUIET
    )
    if(NOT _openmeta_sphinx_import_res EQUAL 0)
      message(FATAL_ERROR
        "OPENMETA_BUILD_SPHINX_DOCS requires Python packages 'sphinx' and 'breathe' for: ${Python_EXECUTABLE}. "
        "Install them (see docs/requirements.txt) and reconfigure.")
    endif()
    set(OPENMETA_SPHINX_FOUND ON)
    set(OPENMETA_BREATHE_FOUND ON)

    set(_openmeta_sphinx_source_dir "${PROJECT_SOURCE_DIR}/docs/sphinx")
    set(_openmeta_sphinx_conf_dir "${CMAKE_CURRENT_BINARY_DIR}/docs/sphinx")
    set(_openmeta_sphinx_out_dir "${_openmeta_docs_root_dir}/html")
    file(MAKE_DIRECTORY "${_openmeta_sphinx_conf_dir}")

    configure_file(
      "${PROJECT_SOURCE_DIR}/docs/sphinx/conf.py.in"
      "${_openmeta_sphinx_conf_dir}/conf.py"
      @ONLY
    )

    add_custom_target(openmeta_docs_sphinx
      DEPENDS openmeta_docs
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${_openmeta_sphinx_out_dir}"
      COMMAND "${Python_EXECUTABLE}" -m sphinx -b html
              -c "${_openmeta_sphinx_conf_dir}"
              "${_openmeta_sphinx_source_dir}"
              "${_openmeta_sphinx_out_dir}"
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      COMMENT "Generating OpenMeta documentation (Sphinx)"
      VERBATIM
    )

    set(_openmeta_install_sphinx_docs_script "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaInstallSphinxDocs.cmake")
    configure_file(
      "${PROJECT_SOURCE_DIR}/cmake/OpenMetaInstallSphinxDocs.cmake.in"
      "${_openmeta_install_sphinx_docs_script}"
      @ONLY
    )
    install(SCRIPT "${_openmeta_install_sphinx_docs_script}")
  endif()
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
