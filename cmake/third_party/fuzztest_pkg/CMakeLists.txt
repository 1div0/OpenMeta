cmake_minimum_required(VERSION 3.20)

project(openmeta_fuzztest_pkg LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(OPENMETA_DEPS_REPOS_ROOT "" CACHE PATH "Path to local deps (must contain fuzztest/, re2/, antlr4/...)")
if(NOT OPENMETA_DEPS_REPOS_ROOT)
  message(FATAL_ERROR
    "Set -DOPENMETA_DEPS_REPOS_ROOT=/path/to/deps (must contain fuzztest/, re2/, antlr4/...).")
endif()

if(NOT EXISTS "${OPENMETA_DEPS_REPOS_ROOT}/fuzztest/CMakeLists.txt")
  message(FATAL_ERROR "Missing `${OPENMETA_DEPS_REPOS_ROOT}/fuzztest` sources.")
endif()

# FuzzTest's CMake build includes RE2 internal headers (e.g. re2/prog.h), which
# are not installed by default by RE2. Provide the RE2 source tree as an include
# path for building FuzzTest.
if(NOT EXISTS "${OPENMETA_DEPS_REPOS_ROOT}/re2/re2/prog.h")
  message(FATAL_ERROR
    "Missing `${OPENMETA_DEPS_REPOS_ROOT}/re2` sources (needed for re2/prog.h).")
endif()
include_directories("${OPENMETA_DEPS_REPOS_ROOT}/re2")

# Offline-only: forbid network downloads.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)
set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "" FORCE)

# Make FuzzTest's BuildDependencies.cmake resolve deps via find_package(), by
# overriding FetchContent sources to local "shim" projects.
set(FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP "${CMAKE_CURRENT_SOURCE_DIR}/shims/abseil-cpp" CACHE PATH "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_RE2 "${CMAKE_CURRENT_SOURCE_DIR}/shims/re2" CACHE PATH "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_GOOGLETEST "${CMAKE_CURRENT_SOURCE_DIR}/shims/googletest" CACHE PATH "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_ANTLR_CPP "${CMAKE_CURRENT_SOURCE_DIR}/shims/antlr_cpp" CACHE PATH "" FORCE)

set(OPENMETA_ANTLR_CPP_REAL_SOURCE_DIR "${OPENMETA_DEPS_REPOS_ROOT}/antlr4/runtime/Cpp" CACHE PATH "" FORCE)
if(NOT EXISTS "${OPENMETA_ANTLR_CPP_REAL_SOURCE_DIR}/runtime/src")
  message(FATAL_ERROR
    "Missing `${OPENMETA_ANTLR_CPP_REAL_SOURCE_DIR}` sources (needed for antlr_cpp_SOURCE_DIR/runtime/src).")
endif()

set(FUZZTEST_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(FUZZTEST_BUILD_FLATBUFFERS OFF CACHE BOOL "" FORCE)
set(FUZZTEST_FUZZING_MODE OFF CACHE BOOL "" FORCE)
set(FUZZTEST_COMPATIBILITY_MODE "" CACHE STRING "" FORCE)

add_subdirectory("${OPENMETA_DEPS_REPOS_ROOT}/fuzztest" "${CMAKE_CURRENT_BINARY_DIR}/_deps/fuzztest-build")

# ------------------------------------------------------------------------------
# Install headers matching the upstream include layout (root-relative includes).
# ------------------------------------------------------------------------------
install(
  DIRECTORY
    "${OPENMETA_DEPS_REPOS_ROOT}/fuzztest/fuzztest"
    "${OPENMETA_DEPS_REPOS_ROOT}/fuzztest/common"
    "${OPENMETA_DEPS_REPOS_ROOT}/fuzztest/centipede"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.inc"
)

# ------------------------------------------------------------------------------
# Export/install FuzzTest targets as a CMake config package.
#
# FuzzTest's CMake creates implementation targets named `fuzztest_<name>` and
# alias targets named `fuzztest::<name>`. For installed packages, we export the
# implementation targets but set EXPORT_NAME to drop the `fuzztest_` prefix so
# consumers keep using `fuzztest::<name>`.
# ------------------------------------------------------------------------------
function(openmeta_collect_buildsystem_targets out_var dir)
  get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)

  foreach(sd IN LISTS _subdirs)
    openmeta_collect_buildsystem_targets(_child_targets "${sd}")
    list(APPEND _targets ${_child_targets})
  endforeach()

  list(REMOVE_DUPLICATES _targets)
  set(${out_var} "${_targets}" PARENT_SCOPE)
endfunction()

openmeta_collect_buildsystem_targets(_all_targets "${CMAKE_CURRENT_SOURCE_DIR}")
set(_fuzztest_lib_targets "")
foreach(t IN LISTS _all_targets)
  if(t MATCHES "^fuzztest_")
    get_target_property(_type "${t}" TYPE)
    if(_type STREQUAL "STATIC_LIBRARY" OR _type STREQUAL "SHARED_LIBRARY" OR _type STREQUAL "INTERFACE_LIBRARY")
      string(REGEX REPLACE "^fuzztest_" "" _export_name "${t}")
      set_target_properties("${t}" PROPERTIES EXPORT_NAME "${_export_name}")
      list(APPEND _fuzztest_lib_targets "${t}")
    endif()
  endif()
endforeach()

list(LENGTH _fuzztest_lib_targets _fuzztest_lib_targets_len)
if(_fuzztest_lib_targets_len EQUAL 0)
  message(FATAL_ERROR "No fuzztest_* library targets detected for export.")
endif()

install(
  TARGETS ${_fuzztest_lib_targets}
  EXPORT fuzztestTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(
  EXPORT fuzztestTargets
  NAMESPACE fuzztest::
  FILE fuzztestTargets.cmake
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/fuzztest"
)

set(_fuzztest_pkg_dir "${CMAKE_INSTALL_LIBDIR}/cmake/fuzztest")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/fuzztestConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/fuzztestConfig.cmake"
  INSTALL_DESTINATION "${_fuzztest_pkg_dir}"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/fuzztestConfigVersion.cmake"
  VERSION 0.0.0
  COMPATIBILITY AnyNewerVersion
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/fuzztestConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/fuzztestConfigVersion.cmake"
  DESTINATION "${_fuzztest_pkg_dir}"
)
