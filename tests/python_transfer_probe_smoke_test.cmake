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
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_python_transfer_probe_smoke")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_jpg "${WORK_DIR}/sample.jpg")

set(_py_code
"from pathlib import Path
import openmeta

p = Path(r'''${_jpg}''')
t = bytearray()
t += b'II*\\x00'
t += (8).to_bytes(4, 'little')
t += (1).to_bytes(2, 'little')
t += (0x0132).to_bytes(2, 'little')
t += (2).to_bytes(2, 'little')
t += (20).to_bytes(4, 'little')
t += (26).to_bytes(4, 'little')
t += (0).to_bytes(4, 'little')
t += b'2000:01:02 03:04:05\\x00'
app1 = b'Exif\\x00\\x00' + bytes(t)
ln = (len(app1) + 2).to_bytes(2, 'big')
p.write_bytes(b'\\xFF\\xD8\\xFF\\xE1' + ln + app1 + b'\\xFF\\xD9')

r = openmeta.transfer_probe(
    str(p),
    format=openmeta.XmpSidecarFormat.Portable,
    include_payloads=False,
    time_patches={'DateTime': '2024:12:31 23:59:59'},
)
assert r['file_status'] == openmeta.TransferFileStatus.Ok, r
assert r['prepare_status'] == openmeta.TransferStatus.Ok, r
assert r['time_patch_status'] == openmeta.TransferStatus.Ok, r
assert r['time_patch_patched_slots'] >= 1, r
assert r['emit_status'] == openmeta.TransferStatus.Ok, r
assert len(r['blocks']) >= 1, r
assert len(r['marker_summary']) >= 1, r

r2 = openmeta.transfer_probe(
    str(p),
    format=openmeta.XmpSidecarFormat.Portable,
    include_payloads=True,
)
assert r2['overall_status'] == openmeta.TransferStatus.UnsafeData, r2
assert r2['error_stage'] == 'api', r2
assert r2['error_code'] == 'unsafe_payloads_forbidden', r2
assert r2['blocks'], r2
assert r2['blocks'][0]['payload'] is None, r2

r3 = openmeta.unsafe_transfer_probe(
    str(p),
    format=openmeta.XmpSidecarFormat.Portable,
    include_payloads=True,
)
assert r3['overall_status'] == openmeta.TransferStatus.Ok, r3
assert r3['blocks'], r3
assert isinstance(r3['blocks'][0]['payload'], (bytes, bytearray)), r3
print('openmeta.transfer_probe smoke ok')
")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -c "${_py_code}"
  RESULT_VARIABLE _rv
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
)
if(NOT _rv EQUAL 0)
  message(FATAL_ERROR
    "python transfer probe smoke failed (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
endif()

message(STATUS "python transfer probe smoke gate passed")
