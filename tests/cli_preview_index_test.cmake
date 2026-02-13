cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED METADUMP_BIN OR METADUMP_BIN STREQUAL "")
  message(FATAL_ERROR "METADUMP_BIN is required")
endif()
if(NOT DEFINED THUMDUMP_BIN OR THUMDUMP_BIN STREQUAL "")
  message(FATAL_ERROR "THUMDUMP_BIN is required")
endif()
if(NOT EXISTS "${METADUMP_BIN}")
  message(FATAL_ERROR "metadump binary not found: ${METADUMP_BIN}")
endif()
if(NOT EXISTS "${THUMDUMP_BIN}")
  message(FATAL_ERROR "thumdump binary not found: ${THUMDUMP_BIN}")
endif()

set(_sample "${SAMPLE_FILE}")
if(_sample STREQUAL "" AND DEFINED ENV{OPENMETA_MULTI_PREVIEW_SAMPLE}
   AND NOT "$ENV{OPENMETA_MULTI_PREVIEW_SAMPLE}" STREQUAL "")
  set(_sample "$ENV{OPENMETA_MULTI_PREVIEW_SAMPLE}")
endif()

if(_sample STREQUAL "")
  message("SKIP: set OPENMETA_MULTI_PREVIEW_SAMPLE (cache var or env) to run CLI preview index test")
  return()
endif()
if(NOT EXISTS "${_sample}")
  message("SKIP: sample file does not exist: ${_sample}")
  return()
endif()

if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_cli_preview_index")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_meta_base "${WORK_DIR}/meta.jpg")
execute_process(
  COMMAND "${METADUMP_BIN}" --extract-preview --force "${_sample}" "${_meta_base}"
  RESULT_VARIABLE _meta_rv
  OUTPUT_VARIABLE _meta_out
  ERROR_VARIABLE _meta_err
)
if(NOT _meta_rv EQUAL 0)
  message(FATAL_ERROR
    "metadump failed (${_meta_rv})\nstdout:\n${_meta_out}\nstderr:\n${_meta_err}")
endif()

if(NOT EXISTS "${WORK_DIR}/meta_1.jpg" OR NOT EXISTS "${WORK_DIR}/meta_2.jpg")
  file(GLOB _meta_glob "${WORK_DIR}/meta*.jpg")
  message(FATAL_ERROR
    "metadump did not create indexed outputs meta_1.jpg/meta_2.jpg\nfound: ${_meta_glob}\nstdout:\n${_meta_out}\nstderr:\n${_meta_err}")
endif()
if(EXISTS "${_meta_base}")
  message(FATAL_ERROR "metadump should not write unsuffixed output in multi-preview mode: ${_meta_base}")
endif()

file(SIZE "${WORK_DIR}/meta_1.jpg" _meta_1_size)
file(SIZE "${WORK_DIR}/meta_2.jpg" _meta_2_size)
if(_meta_1_size EQUAL 0 OR _meta_2_size EQUAL 0)
  message(FATAL_ERROR "metadump indexed outputs are empty")
endif()

set(_thumb_base "${WORK_DIR}/thumb.jpg")
execute_process(
  COMMAND "${THUMDUMP_BIN}" --force "${_sample}" "${_thumb_base}"
  RESULT_VARIABLE _thumb_rv
  OUTPUT_VARIABLE _thumb_out
  ERROR_VARIABLE _thumb_err
)
if(NOT _thumb_rv EQUAL 0)
  message(FATAL_ERROR
    "thumdump failed (${_thumb_rv})\nstdout:\n${_thumb_out}\nstderr:\n${_thumb_err}")
endif()

if(NOT EXISTS "${WORK_DIR}/thumb_1.jpg" OR NOT EXISTS "${WORK_DIR}/thumb_2.jpg")
  file(GLOB _thumb_glob "${WORK_DIR}/thumb*.jpg")
  message(FATAL_ERROR
    "thumdump did not create indexed outputs thumb_1.jpg/thumb_2.jpg\nfound: ${_thumb_glob}\nstdout:\n${_thumb_out}\nstderr:\n${_thumb_err}")
endif()
if(EXISTS "${_thumb_base}")
  message(FATAL_ERROR "thumdump should not write unsuffixed output in multi-preview mode: ${_thumb_base}")
endif()

file(SIZE "${WORK_DIR}/thumb_1.jpg" _thumb_1_size)
file(SIZE "${WORK_DIR}/thumb_2.jpg" _thumb_2_size)
if(_thumb_1_size EQUAL 0 OR _thumb_2_size EQUAL 0)
  message(FATAL_ERROR "thumdump indexed outputs are empty")
endif()

message(STATUS "CLI preview index test passed for sample: ${_sample}")
