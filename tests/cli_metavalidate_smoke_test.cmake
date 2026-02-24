cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED METAVALIDATE_BIN OR METAVALIDATE_BIN STREQUAL "")
  message(FATAL_ERROR "METAVALIDATE_BIN is required")
endif()
if(NOT EXISTS "${METAVALIDATE_BIN}")
  message(FATAL_ERROR "metavalidate binary not found: ${METAVALIDATE_BIN}")
endif()

if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_cli_metavalidate_smoke")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_jpg "${WORK_DIR}/sample.jpg")
set(_xmp "${WORK_DIR}/sample.xmp")

# Minimal JPEG SOI/EOI.
string(ASCII 255 216 255 217 _jpg_bytes)
file(WRITE "${_jpg}" "${_jpg_bytes}")
# Intentionally malformed XMP to trigger warning (output_truncated).
file(WRITE "${_xmp}" "<x:xmpmeta><rdf:RDF>")

execute_process(
  COMMAND "${METAVALIDATE_BIN}" --no-build-info "${_jpg}"
  RESULT_VARIABLE _rv_ok
  OUTPUT_VARIABLE _out_ok
  ERROR_VARIABLE _err_ok
)
if(NOT _rv_ok EQUAL 0)
  message(FATAL_ERROR
    "metavalidate basic run failed (${_rv_ok})\nstdout:\n${_out_ok}\nstderr:\n${_err_ok}")
endif()
if(NOT _out_ok MATCHES "result=ok")
  message(FATAL_ERROR
    "metavalidate basic run did not report result=ok\nstdout:\n${_out_ok}\nstderr:\n${_err_ok}")
endif()

execute_process(
  COMMAND "${METAVALIDATE_BIN}" --no-build-info --json "${_jpg}"
  RESULT_VARIABLE _rv_json
  OUTPUT_VARIABLE _out_json
  ERROR_VARIABLE _err_json
)
if(NOT _rv_json EQUAL 0)
  message(FATAL_ERROR
    "metavalidate --json failed (${_rv_json})\nstdout:\n${_out_json}\nstderr:\n${_err_json}")
endif()
if(NOT _out_json MATCHES "\"summary\"")
  message(FATAL_ERROR
    "metavalidate --json output missing summary field\nstdout:\n${_out_json}\nstderr:\n${_err_json}")
endif()

execute_process(
  COMMAND "${METAVALIDATE_BIN}" --no-build-info --xmp-sidecar --strict "${_jpg}"
  RESULT_VARIABLE _rv_strict
  OUTPUT_VARIABLE _out_strict
  ERROR_VARIABLE _err_strict
)
if(NOT _rv_strict EQUAL 1)
  message(FATAL_ERROR
    "metavalidate strict warning promotion expected exit=1, got ${_rv_strict}\nstdout:\n${_out_strict}\nstderr:\n${_err_strict}")
endif()
if(NOT _out_strict MATCHES "output_truncated")
  message(FATAL_ERROR
    "metavalidate strict run missing output_truncated signal\nstdout:\n${_out_strict}\nstderr:\n${_err_strict}")
endif()

message(STATUS "metavalidate smoke gate passed")
