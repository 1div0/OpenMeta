cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED OPENMETA_PYTHON_EXECUTABLE OR OPENMETA_PYTHON_EXECUTABLE STREQUAL "")
  message(FATAL_ERROR "OPENMETA_PYTHON_EXECUTABLE is required")
endif()
if(NOT EXISTS "${OPENMETA_PYTHON_EXECUTABLE}")
  message(FATAL_ERROR "Python executable not found: ${OPENMETA_PYTHON_EXECUTABLE}")
endif()

if(NOT DEFINED OPENMETA_PYTHONPATH OR OPENMETA_PYTHONPATH STREQUAL "")
  message(FATAL_ERROR "OPENMETA_PYTHONPATH is required")
endif()

if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_python_metatransfer_edit_smoke")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_src_jpg "${WORK_DIR}/source.jpg")
set(_target_jpg "${WORK_DIR}/target.jpg")
set(_edited_jpg "${WORK_DIR}/edited.jpg")
set(_target_tif "${WORK_DIR}/target.tif")
set(_edited_tif "${WORK_DIR}/edited.tif")
set(_check_tiff_py "${WORK_DIR}/check_tiff.py")

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; t=bytearray(); t+=b'II*\\x00'; t+=(8).to_bytes(4,'little'); t+=(1).to_bytes(2,'little'); t+=(0x0132).to_bytes(2,'little'); t+=(2).to_bytes(2,'little'); t+=(20).to_bytes(4,'little'); t+=(26).to_bytes(4,'little'); t+=(0).to_bytes(4,'little'); t+=b'2000:01:02 03:04:05\\x00'; app1=b'Exif\\x00\\x00'+bytes(t); ln=(len(app1)+2).to_bytes(2,'big'); Path(r'''${_src_jpg}''').write_bytes(b'\\xFF\\xD8\\xFF\\xE1'+ln+app1+b'\\xFF\\xD9')"
  RESULT_VARIABLE _rv_src
  OUTPUT_VARIABLE _out_src
  ERROR_VARIABLE _err_src
)
if(NOT _rv_src EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer source fixture (${_rv_src})\nstdout:\n${_out_src}\nstderr:\n${_err_src}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; Path(r'''${_target_jpg}''').write_bytes(bytes([255,216,255,217]))"
  RESULT_VARIABLE _rv_target_jpg
  OUTPUT_VARIABLE _out_target_jpg
  ERROR_VARIABLE _err_target_jpg
)
if(NOT _rv_target_jpg EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target jpeg fixture (${_rv_target_jpg})\nstdout:\n${_out_target_jpg}\nstderr:\n${_err_target_jpg}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; b=bytearray(); b+=b'II'; b+=(42).to_bytes(2,'little'); b+=(8).to_bytes(4,'little'); b+=(0).to_bytes(2,'little'); b+=(0).to_bytes(4,'little'); Path(r'''${_target_tif}''').write_bytes(bytes(b))"
  RESULT_VARIABLE _rv_target_tif
  OUTPUT_VARIABLE _out_target_tif
  ERROR_VARIABLE _err_target_tif
)
if(NOT _rv_target_tif EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target tiff fixture (${_rv_target_tif})\nstdout:\n${_out_target_tif}\nstderr:\n${_err_target_tif}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --time-patch "DateTime=2024:12:31 23:59:59"
          --target-jpeg "${_target_jpg}"
          -o "${_edited_jpg}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_jpg
  OUTPUT_VARIABLE _out_jpg
  ERROR_VARIABLE _err_jpg
)
if(NOT _rv_jpg EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jpeg edit failed (${_rv_jpg})\nstdout:\n${_out_jpg}\nstderr:\n${_err_jpg}")
endif()
if(NOT _out_jpg MATCHES "edit_plan: status=ok")
  message(FATAL_ERROR
    "python metatransfer jpeg edit missing plan ok\nstdout:\n${_out_jpg}\nstderr:\n${_err_jpg}")
endif()
if(NOT _out_jpg MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer jpeg edit missing apply ok\nstdout:\n${_out_jpg}\nstderr:\n${_err_jpg}")
endif()
if(NOT EXISTS "${_edited_jpg}")
  message(FATAL_ERROR
    "python metatransfer jpeg edit did not write output\nstdout:\n${_out_jpg}\nstderr:\n${_err_jpg}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; b=Path(r'''${_edited_jpg}''').read_bytes(); assert b[:2]==b'\\xff\\xd8'; assert b.find(b'Exif\\x00\\x00')!=-1; assert b.find(b'2024:12:31 23:59:59\\x00')!=-1"
  RESULT_VARIABLE _rv_jpg_check
  OUTPUT_VARIABLE _out_jpg_check
  ERROR_VARIABLE _err_jpg_check
)
if(NOT _rv_jpg_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jpeg output check failed (${_rv_jpg_check})\nstdout:\n${_out_jpg_check}\nstderr:\n${_err_jpg_check}")
endif()

file(WRITE "${_check_tiff_py}" [=[
import sys
from pathlib import Path

def read_u16(b, off):
    return int.from_bytes(b[off:off+2], "little", signed=False)

def read_u32(b, off):
    return int.from_bytes(b[off:off+4], "little", signed=False)

path = Path(sys.argv[1])
expect_dt = sys.argv[2]
b = path.read_bytes()
if len(b) < 8 or b[:2] != b"II" or read_u16(b, 2) != 42:
    raise SystemExit("invalid little-endian classic TIFF")
ifd0_off = read_u32(b, 4)
count = read_u16(b, ifd0_off)
p = ifd0_off + 2
tags = {}
for i in range(count):
    e = p + i * 12
    tag = read_u16(b, e + 0)
    typ = read_u16(b, e + 2)
    cnt = read_u32(b, e + 4)
    raw = b[e + 8:e + 12]
    tags[tag] = (typ, cnt, raw)
if 0x0132 not in tags:
    raise SystemExit("missing DateTime")
if 700 not in tags:
    raise SystemExit("missing XMP tag 700")
typ, cnt, raw = tags[0x0132]
if typ != 2 or cnt != 20:
    raise SystemExit("DateTime type/count mismatch")
off = read_u32(raw, 0)
dt = b[off:off+20].split(b"\x00", 1)[0].decode("ascii", "ignore")
if dt != expect_dt:
    raise SystemExit(f"DateTime mismatch: {dt!r}")
]=])

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --time-patch "DateTime=2024:12:31 23:59:59"
          --target-tiff "${_target_tif}"
          -o "${_edited_tif}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_tif
  OUTPUT_VARIABLE _out_tif
  ERROR_VARIABLE _err_tif
)
if(NOT _rv_tif EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer tiff edit failed (${_rv_tif})\nstdout:\n${_out_tif}\nstderr:\n${_err_tif}")
endif()
if(NOT _out_tif MATCHES "edit_plan: status=ok")
  message(FATAL_ERROR
    "python metatransfer tiff edit missing plan ok\nstdout:\n${_out_tif}\nstderr:\n${_err_tif}")
endif()
if(NOT _out_tif MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer tiff edit missing apply ok\nstdout:\n${_out_tif}\nstderr:\n${_err_tif}")
endif()
if(NOT EXISTS "${_edited_tif}")
  message(FATAL_ERROR
    "python metatransfer tiff edit did not write output\nstdout:\n${_out_tif}\nstderr:\n${_err_tif}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_tiff_py}" "${_edited_tif}" "2024:12:31 23:59:59"
  RESULT_VARIABLE _rv_tif_check
  OUTPUT_VARIABLE _out_tif_check
  ERROR_VARIABLE _err_tif_check
)
if(NOT _rv_tif_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer tiff output check failed (${_rv_tif_check})\nstdout:\n${_out_tif_check}\nstderr:\n${_err_tif_check}")
endif()

message(STATUS "python metatransfer edit smoke gate passed")
