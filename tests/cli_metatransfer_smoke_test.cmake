cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED METATRANSFER_BIN OR METATRANSFER_BIN STREQUAL "")
  message(FATAL_ERROR "METATRANSFER_BIN is required")
endif()
if(NOT EXISTS "${METATRANSFER_BIN}")
  message(FATAL_ERROR "metatransfer binary not found: ${METATRANSFER_BIN}")
endif()

if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_cli_metatransfer_smoke")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_jpg "${WORK_DIR}/sample.jpg")
set(_dump_dir "${WORK_DIR}/payloads")
set(_edited_jpg "${WORK_DIR}/edited.jpg")
file(MAKE_DIRECTORY "${_dump_dir}")

# Minimal JPEG with APP1 Exif payload:
# SOI + APP1(Exif + tiny TIFF IFD0 with DateTime ASCII tag) + EOI.
execute_process(
  COMMAND python3 -c
    "from pathlib import Path; t=bytearray(); t+=b'II*\\x00'; t+=(8).to_bytes(4,'little'); t+=(1).to_bytes(2,'little'); t+=(0x0132).to_bytes(2,'little'); t+=(2).to_bytes(2,'little'); t+=(20).to_bytes(4,'little'); t+=(26).to_bytes(4,'little'); t+=(0).to_bytes(4,'little'); t+=b'2000:01:02 03:04:05\\x00'; app1=b'Exif\\x00\\x00'+bytes(t); ln=(len(app1)+2).to_bytes(2,'big'); jpg=b'\\xFF\\xD8\\xFF\\xE1'+ln+app1+b'\\xFF\\xD9'; Path(r'''${_jpg}''').write_bytes(jpg)"
  RESULT_VARIABLE _rv_write
  OUTPUT_VARIABLE _out_write
  ERROR_VARIABLE _err_write
)
if(NOT _rv_write EQUAL 0)
  message(FATAL_ERROR
    "failed to write metatransfer fixture (${_rv_write})\nstdout:\n${_out_write}\nstderr:\n${_err_write}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --time-patch "DateTime=2024:12:31 23:59:59"
          "${_jpg}"
  RESULT_VARIABLE _rv_probe
  OUTPUT_VARIABLE _out_probe
  ERROR_VARIABLE _err_probe
)
if(NOT _rv_probe EQUAL 0)
  message(FATAL_ERROR
    "metatransfer probe failed (${_rv_probe})\nstdout:\n${_out_probe}\nstderr:\n${_err_probe}")
endif()
if(NOT _out_probe MATCHES "prepare: status=ok")
  message(FATAL_ERROR
    "metatransfer probe missing prepare ok\nstdout:\n${_out_probe}\nstderr:\n${_err_probe}")
endif()
if(NOT _out_probe MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "metatransfer probe missing emit ok\nstdout:\n${_out_probe}\nstderr:\n${_err_probe}")
endif()
if(NOT _out_probe MATCHES "time_patch: status=ok patched=1")
  message(FATAL_ERROR
    "metatransfer probe missing time_patch patched=1\nstdout:\n${_out_probe}\nstderr:\n${_err_probe}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --mode auto --dry-run
          --time-patch "DateTime=2024:12:31 23:59:59"
          "${_jpg}"
  RESULT_VARIABLE _rv_dry
  OUTPUT_VARIABLE _out_dry
  ERROR_VARIABLE _err_dry
)
if(NOT _rv_dry EQUAL 0)
  message(FATAL_ERROR
    "metatransfer dry-run failed (${_rv_dry})\nstdout:\n${_out_dry}\nstderr:\n${_err_dry}")
endif()
if(NOT _out_dry MATCHES "edit_plan: status=ok")
  message(FATAL_ERROR
    "metatransfer dry-run missing edit_plan ok\nstdout:\n${_out_dry}\nstderr:\n${_err_dry}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --mode metadata_rewrite
          --time-patch "DateTime=2024:12:31 23:59:59"
          --output "${_edited_jpg}" --force
          "${_jpg}"
  RESULT_VARIABLE _rv_edit
  OUTPUT_VARIABLE _out_edit
  ERROR_VARIABLE _err_edit
)
if(NOT _rv_edit EQUAL 0)
  message(FATAL_ERROR
    "metatransfer edit apply failed (${_rv_edit})\nstdout:\n${_out_edit}\nstderr:\n${_err_edit}")
endif()
if(NOT EXISTS "${_edited_jpg}")
  message(FATAL_ERROR
    "metatransfer did not write edited output\nstdout:\n${_out_edit}\nstderr:\n${_err_edit}")
endif()
file(SIZE "${_edited_jpg}" _edited_size)
if(_edited_size LESS 10)
  message(FATAL_ERROR
    "metatransfer wrote suspiciously small edited output (${_edited_size})\nstdout:\n${_out_edit}\nstderr:\n${_err_edit}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info --unsafe-write-payloads --out-dir "${_dump_dir}" --force "${_jpg}"
  RESULT_VARIABLE _rv_dump
  OUTPUT_VARIABLE _out_dump
  ERROR_VARIABLE _err_dump
)
if(NOT _rv_dump EQUAL 0)
  message(FATAL_ERROR
    "metatransfer payload dump failed (${_rv_dump})\nstdout:\n${_out_dump}\nstderr:\n${_err_dump}")
endif()

file(GLOB _dump_files "${_dump_dir}/*.bin")
list(LENGTH _dump_files _dump_count)
if(_dump_count LESS 1)
  message(FATAL_ERROR
    "metatransfer did not write payload dumps\nstdout:\n${_out_dump}\nstderr:\n${_err_dump}")
endif()

message(STATUS "metatransfer smoke gate passed")
