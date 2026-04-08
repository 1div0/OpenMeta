cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED OPENMETA_TESTS_BIN OR OPENMETA_TESTS_BIN STREQUAL "")
  message(FATAL_ERROR "OPENMETA_TESTS_BIN is required")
endif()
if(NOT EXISTS "${OPENMETA_TESTS_BIN}")
  message(FATAL_ERROR "openmeta_tests binary not found: ${OPENMETA_TESTS_BIN}")
endif()
if(NOT DEFINED METATRANSFER_BIN OR METATRANSFER_BIN STREQUAL "")
  message(FATAL_ERROR "METATRANSFER_BIN is required")
endif()
if(NOT EXISTS "${METATRANSFER_BIN}")
  message(FATAL_ERROR "metatransfer binary not found: ${METATRANSFER_BIN}")
endif()

if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_transfer_release_gate")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_gtest_filter
  "MetadataTransferApi.*:XmpDump.*:ExrAdapter.*:DngSdkAdapter.*")

execute_process(
  COMMAND "${OPENMETA_TESTS_BIN}" "--gtest_filter=${_gtest_filter}"
  WORKING_DIRECTORY "${WORK_DIR}"
  RESULT_VARIABLE _rv_tests
  OUTPUT_VARIABLE _out_tests
  ERROR_VARIABLE _err_tests
)
if(NOT _rv_tests EQUAL 0)
  message(FATAL_ERROR
    "transfer release gate gtests failed (${_rv_tests})\n"
    "stdout:\n${_out_tests}\n"
    "stderr:\n${_err_tests}")
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND}
    "-DMETATRANSFER_BIN=${METATRANSFER_BIN}"
    "-DWORK_DIR=${WORK_DIR}/cli_metatransfer_smoke"
    -P "${CMAKE_CURRENT_LIST_DIR}/cli_metatransfer_smoke_test.cmake"
  RESULT_VARIABLE _rv_cli
  OUTPUT_VARIABLE _out_cli
  ERROR_VARIABLE _err_cli
)
if(NOT _rv_cli EQUAL 0)
  message(FATAL_ERROR
    "transfer release gate CLI smoke failed (${_rv_cli})\n"
    "stdout:\n${_out_cli}\n"
    "stderr:\n${_err_cli}")
endif()

set(_have_python ON)
if(NOT DEFINED OPENMETA_PYTHON_EXECUTABLE
   OR OPENMETA_PYTHON_EXECUTABLE STREQUAL "")
  set(_have_python OFF)
endif()
if(NOT DEFINED OPENMETA_PYTHONPATH OR OPENMETA_PYTHONPATH STREQUAL "")
  set(_have_python OFF)
endif()
if(_have_python AND NOT EXISTS "${OPENMETA_PYTHON_EXECUTABLE}")
  message(FATAL_ERROR
    "Python executable not found for transfer release gate: "
    "${OPENMETA_PYTHON_EXECUTABLE}")
endif()

if(_have_python)
  execute_process(
    COMMAND ${CMAKE_COMMAND}
      "-DOPENMETA_PYTHON_EXECUTABLE=${OPENMETA_PYTHON_EXECUTABLE}"
      "-DOPENMETA_PYTHONPATH=${OPENMETA_PYTHONPATH}"
      "-DWORK_DIR=${WORK_DIR}/python_transfer_probe_smoke"
      -P "${CMAKE_CURRENT_LIST_DIR}/python_transfer_probe_smoke_test.cmake"
    RESULT_VARIABLE _rv_py_probe
    OUTPUT_VARIABLE _out_py_probe
    ERROR_VARIABLE _err_py_probe
  )
  if(NOT _rv_py_probe EQUAL 0)
    message(FATAL_ERROR
      "transfer release gate python probe smoke failed (${_rv_py_probe})\n"
      "stdout:\n${_out_py_probe}\n"
      "stderr:\n${_err_py_probe}")
  endif()

  execute_process(
    COMMAND ${CMAKE_COMMAND}
      "-DOPENMETA_PYTHON_EXECUTABLE=${OPENMETA_PYTHON_EXECUTABLE}"
      "-DOPENMETA_PYTHONPATH=${OPENMETA_PYTHONPATH}"
      "-DWORK_DIR=${WORK_DIR}/python_metatransfer_edit_smoke"
      -P
      "${CMAKE_CURRENT_LIST_DIR}/python_metatransfer_edit_smoke_test.cmake"
    RESULT_VARIABLE _rv_py_edit
    OUTPUT_VARIABLE _out_py_edit
    ERROR_VARIABLE _err_py_edit
  )
  if(NOT _rv_py_edit EQUAL 0)
    message(FATAL_ERROR
      "transfer release gate python edit smoke failed (${_rv_py_edit})\n"
      "stdout:\n${_out_py_edit}\n"
      "stderr:\n${_err_py_edit}")
  endif()
endif()
