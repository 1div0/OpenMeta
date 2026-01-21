include_guard(GLOBAL)

function(openmeta_resolve_deps_root out_var)
  set(resolved "")

  if(OPENMETA_DEPS_ROOT)
    if(IS_ABSOLUTE "${OPENMETA_DEPS_ROOT}")
      if(EXISTS "${OPENMETA_DEPS_ROOT}")
        set(resolved "${OPENMETA_DEPS_ROOT}")
      endif()
    else()
      get_filename_component(candidate_project "${OPENMETA_DEPS_ROOT}" ABSOLUTE
        BASE_DIR "${PROJECT_SOURCE_DIR}")
      get_filename_component(candidate_parent "${OPENMETA_DEPS_ROOT}" ABSOLUTE
        BASE_DIR "${PROJECT_SOURCE_DIR}/..")

      if(EXISTS "${candidate_project}")
        set(resolved "${candidate_project}")
      elseif(EXISTS "${candidate_parent}")
        set(resolved "${candidate_parent}")
      endif()
    endif()
  else()
    set(candidates
      "${PROJECT_SOURCE_DIR}/../OpenMeta-internal/ext"
      "${PROJECT_SOURCE_DIR}/../openmeta-internal/ext"
      "${PROJECT_SOURCE_DIR}/OpenMeta-internal/ext"
      "${PROJECT_SOURCE_DIR}/openmeta-internal/ext"
      "${PROJECT_SOURCE_DIR}/ext"
      "${PROJECT_SOURCE_DIR}/../ext"
    )

    foreach(candidate IN LISTS candidates)
      if(EXISTS "${candidate}/googletest/CMakeLists.txt")
        set(resolved "${candidate}")
        break()
      endif()
      if(EXISTS "${candidate}/fuzztest/CMakeLists.txt")
        set(resolved "${candidate}")
        break()
      endif()
    endforeach()
  endif()

  set(${out_var} "${resolved}" PARENT_SCOPE)
endfunction()

function(openmeta_require_dependency_sources deps_root dep_name rel_path)
  if(NOT deps_root)
    message(FATAL_ERROR
      "${dep_name} sources not found (deps root is empty). "
      "Set OPENMETA_DEPS_ROOT or enable OPENMETA_FETCH_DEPS=ON.")
  endif()

  if(NOT EXISTS "${deps_root}/${rel_path}/CMakeLists.txt")
    message(FATAL_ERROR
      "${dep_name} sources not found (expected `${deps_root}/${rel_path}`). "
      "Set OPENMETA_DEPS_ROOT to a directory containing `${rel_path}` "
      "or enable OPENMETA_FETCH_DEPS=ON.")
  endif()
endfunction()

function(openmeta_add_googletest deps_root)
  if(TARGET gtest_main)
    return()
  endif()

  find_package(GTest CONFIG QUIET)
  if(GTest_FOUND)
    if(TARGET GTest::gtest_main AND NOT TARGET gtest_main)
      add_library(gtest_main ALIAS GTest::gtest_main)
      return()
    endif()
    message(FATAL_ERROR "GTest was found, but no `GTest::gtest_main` target is available.")
  endif()

  find_package(GTest QUIET)
  if(GTest_FOUND)
    if(TARGET GTest::gtest_main AND NOT TARGET gtest_main)
      add_library(gtest_main ALIAS GTest::gtest_main)
      return()
    endif()
    if(TARGET GTest::Main AND NOT TARGET gtest_main)
      add_library(gtest_main ALIAS GTest::Main)
      return()
    endif()
    message(FATAL_ERROR "GTest was found, but no usable main target is available.")
  endif()

  if(deps_root AND EXISTS "${deps_root}/googletest/CMakeLists.txt")
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory("${deps_root}/googletest" "${CMAKE_BINARY_DIR}/_deps/googletest-build")
    return()
  endif()

  if(NOT OPENMETA_FETCH_DEPS)
    message(FATAL_ERROR
      "GoogleTest not found. Install a system package providing `GTest::gtest_main`, "
      "or provide sources under OPENMETA_DEPS_ROOT (expected `googletest/`), "
      "or enable OPENMETA_FETCH_DEPS=ON.")
  endif()

  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG 6910c9d9165801d8827d628cb72eb7ea9dd538c5
  )
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endfunction()

function(openmeta_add_fuzztest deps_root)
  if(TARGET fuzztest::fuzztest)
    return()
  endif()

  find_package(fuzztest CONFIG QUIET)
  if(fuzztest_FOUND)
    return()
  endif()

  set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

  if(deps_root)
    if(EXISTS "${deps_root}/abseil-cpp/CMakeLists.txt")
      set("FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP" "${deps_root}/abseil-cpp" CACHE PATH "" FORCE)
    endif()
    if(EXISTS "${deps_root}/re2/CMakeLists.txt")
      set("FETCHCONTENT_SOURCE_DIR_RE2" "${deps_root}/re2" CACHE PATH "" FORCE)
    endif()
    if(EXISTS "${deps_root}/googletest/CMakeLists.txt")
      set("FETCHCONTENT_SOURCE_DIR_GOOGLETEST" "${deps_root}/googletest" CACHE PATH "" FORCE)
    endif()
    if(EXISTS "${deps_root}/antlr4/runtime/Cpp/CMakeLists.txt")
      set("FETCHCONTENT_SOURCE_DIR_ANTLR_CPP" "${deps_root}/antlr4/runtime/Cpp" CACHE PATH "" FORCE)
    endif()
  endif()

  if(deps_root AND EXISTS "${deps_root}/fuzztest/CMakeLists.txt")
    if(NOT OPENMETA_FETCH_DEPS)
      openmeta_require_dependency_sources("${deps_root}" "Abseil" "abseil-cpp")
      openmeta_require_dependency_sources("${deps_root}" "RE2" "re2")
      openmeta_require_dependency_sources("${deps_root}" "GoogleTest" "googletest")
      openmeta_require_dependency_sources("${deps_root}" "ANTLR4 runtime" "antlr4/runtime/Cpp")
    endif()

    # The ANTLR4 runtime sets VERSION/SOVERSION on its shared library, which makes
    # CMake create symlinks (not allowed in some sandboxed environments). We only
    # need the static runtime for FuzzTest.
    set(ANTLR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ANTLR_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)

    set(FUZZTEST_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(FUZZTEST_BUILD_FLATBUFFERS OFF CACHE BOOL "" FORCE)
    set(FUZZTEST_FUZZING_MODE ${OPENMETA_FUZZTEST_FUZZING_MODE} CACHE BOOL "" FORCE)
    set(FUZZTEST_COMPATIBILITY_MODE "" CACHE STRING "" FORCE)

    add_subdirectory("${deps_root}/fuzztest" "${CMAKE_BINARY_DIR}/_deps/fuzztest-build")
    return()
  endif()

  if(NOT OPENMETA_FETCH_DEPS)
    message(FATAL_ERROR
      "FuzzTest not found. Provide sources under OPENMETA_DEPS_ROOT (expected `fuzztest/`) "
      "or enable OPENMETA_FETCH_DEPS=ON.")
  endif()

  include(FetchContent)
  FetchContent_Declare(
    fuzztest
    GIT_REPOSITORY https://github.com/google/fuzztest.git
    GIT_TAG 5a702743bbf29e08f78b4077ccc1d119d4af785b
  )
  FetchContent_MakeAvailable(fuzztest)
endfunction()
