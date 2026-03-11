cmake_minimum_required(VERSION 3.20)

foreach(_var IN ITEMS METAREAD_BIN METADUMP_BIN THUMDUMP_BIN
                      METAVALIDATE_BIN METATRANSFER_BIN)
  if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
    message(FATAL_ERROR "${_var} is required")
  endif()
  if(NOT EXISTS "${${_var}}")
    message(FATAL_ERROR "${_var} binary not found: ${${_var}}")
  endif()
endforeach()

if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_cli_numeric_parse_smoke")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_jpg "${WORK_DIR}/sample.jpg")
string(ASCII 255 216 255 217 _jpg_bytes)
file(WRITE "${_jpg}" "${_jpg_bytes}")

set(_overflow "4294967296")

function(_expect_invalid _bin _opt _needle)
  execute_process(
    COMMAND "${_bin}" --no-build-info "${_opt}" "${_overflow}" "${_jpg}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 2)
    message(FATAL_ERROR
      "${_opt} overflow expected exit=2, got ${_rv}\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _err MATCHES "${_needle}")
    message(FATAL_ERROR
      "${_opt} overflow missing expected error '${_needle}'\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
endfunction()

_expect_invalid("${METAREAD_BIN}" "--max-elements"
                "invalid --max-elements value")
_expect_invalid("${METADUMP_BIN}" "--max-entries"
                "invalid --max-entries value")
_expect_invalid("${THUMDUMP_BIN}" "--max-preview-ifds"
                "invalid --max-preview-ifds value")
_expect_invalid("${METAVALIDATE_BIN}" "--max-ccm-fields"
                "invalid --max-ccm-fields value")
_expect_invalid("${METATRANSFER_BIN}" "--emit-repeat"
                "invalid --emit-repeat value")

message(STATUS "CLI numeric parse smoke gate passed")
