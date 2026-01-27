include_guard(GLOBAL)

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module OPTIONAL_COMPONENTS Development.SABIModule)
find_package(nanobind CONFIG REQUIRED)

set(OPENMETA_PYTHON_VERSION "${Python_VERSION}" PARENT_SCOPE)
set(OPENMETA_NANOBIND_FOUND 1 PARENT_SCOPE)

nanobind_add_module(openmeta_python
  src/openmeta_module.cc
)

set_target_properties(openmeta_python PROPERTIES OUTPUT_NAME "_openmeta")

target_compile_definitions(openmeta_python PRIVATE OPENMETA_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(openmeta_python PRIVATE ${OPENMETA_LIB_TARGET})
openmeta_apply_core_deps(openmeta_python)

target_include_directories(
  openmeta_python
  PRIVATE
    "${PROJECT_SOURCE_DIR}/src/include"
)

# Make the build tree importable via: PYTHONPATH=<build>/python
set(_openmeta_py_build_dir "${CMAKE_BINARY_DIR}/python/openmeta")
file(MAKE_DIRECTORY "${_openmeta_py_build_dir}")
file(MAKE_DIRECTORY "${_openmeta_py_build_dir}/python")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/openmeta/__init__.py"
  "${CMAKE_BINARY_DIR}/python/openmeta/__init__.py"
  COPYONLY
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/openmeta/python/__init__.py"
  "${CMAKE_BINARY_DIR}/python/openmeta/python/__init__.py"
  COPYONLY
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/openmeta/python/metaread.py"
  "${CMAKE_BINARY_DIR}/python/openmeta/python/metaread.py"
  COPYONLY
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/openmeta/python/openmeta_stats.py"
  "${CMAKE_BINARY_DIR}/python/openmeta/python/openmeta_stats.py"
  COPYONLY
)

set_target_properties(openmeta_python PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${_openmeta_py_build_dir}"
  RUNTIME_OUTPUT_DIRECTORY "${_openmeta_py_build_dir}"
)

# Install into the wheel/site-packages root.
set(_openmeta_py_install_dir "openmeta")
if(DEFINED SKBUILD_PLATLIB_DIR)
  set(_openmeta_py_install_dir "${SKBUILD_PLATLIB_DIR}/openmeta")
endif()

set(_openmeta_install_python_module ON)
# When building a wheel (via scikit-build-core), we must install the module
# into the wheel staging area (SKBUILD_PLATLIB_DIR).
#
# But for a normal CMake install with OPENMETA_BUILD_WHEEL enabled, the desired
# output is the wheel file itself (installed by the top-level install script),
# not a loose extension module in the install prefix.
if(OPENMETA_BUILD_WHEEL AND NOT DEFINED SKBUILD_PLATLIB_DIR)
  set(_openmeta_install_python_module OFF)
endif()

if(_openmeta_install_python_module)
  install(
    TARGETS openmeta_python
    LIBRARY DESTINATION "${_openmeta_py_install_dir}"
    RUNTIME DESTINATION "${_openmeta_py_install_dir}"
  )
  install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/openmeta/__init__.py"
    DESTINATION "${_openmeta_py_install_dir}"
  )

  install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/openmeta/python"
    DESTINATION "${_openmeta_py_install_dir}"
    FILES_MATCHING PATTERN "*.py"
  )
endif()
