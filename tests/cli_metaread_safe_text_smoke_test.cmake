cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED METAREAD_BIN OR METAREAD_BIN STREQUAL "")
  message(FATAL_ERROR "METAREAD_BIN is required")
endif()
if(NOT EXISTS "${METAREAD_BIN}")
  message(FATAL_ERROR "metaread binary not found: ${METAREAD_BIN}")
endif()

if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_cli_metaread_safe_text_smoke")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_jpg "${WORK_DIR}/unsafe_text.jpg")

# JPEG with APP1 Exif containing IFD0:Make (0x010F, ASCII) = "A\\x01\\0".
# This should be rendered as a safe corrupted-text placeholder in metaread.
execute_process(
  COMMAND python3 -c
    "from pathlib import Path; Path(r'''${_jpg}''').write_bytes(bytes([255,216,255,225,0,34,69,120,105,102,0,0,73,73,42,0,8,0,0,0,1,0,15,1,2,0,3,0,0,0,65,1,0,0,0,0,0,0,255,217]))"
  RESULT_VARIABLE _rv_write
  OUTPUT_VARIABLE _out_write
  ERROR_VARIABLE _err_write
)
if(NOT _rv_write EQUAL 0)
  message(FATAL_ERROR
    "failed to write unsafe-text JPEG fixture (${_rv_write})\nstdout:\n${_out_write}\nstderr:\n${_err_write}")
endif()

execute_process(
  COMMAND "${METAREAD_BIN}" --no-build-info "${_jpg}"
  RESULT_VARIABLE _rv
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
)

if(NOT _rv EQUAL 0)
  message(FATAL_ERROR
    "metaread safe-text smoke failed (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
endif()

if(NOT _out MATCHES "CORRUPTED_TEXT")
  message(FATAL_ERROR
    "metaread output missing CORRUPTED_TEXT placeholder\nstdout:\n${_out}\nstderr:\n${_err}")
endif()

message(STATUS "metaread safe-text smoke gate passed")
