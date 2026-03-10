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
set(_c2pa_jpg "${WORK_DIR}/sample_c2pa.jpg")
set(_c2pa_signed_jumb "${WORK_DIR}/signed_c2pa.jumb")
set(_c2pa_manifest "${WORK_DIR}/manifest.cbor")
set(_c2pa_cert "${WORK_DIR}/cert.der")
set(_c2pa_handoff "${WORK_DIR}/handoff.omc2ph")
set(_c2pa_signed_package "${WORK_DIR}/signed_package.omc2ps")
set(_c2pa_from_package "${WORK_DIR}/edited_from_package.jpg")
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
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; jumd=b'c2pa\\x00'; box=lambda t,p:(8+len(p)).to_bytes(4,'big')+t+p; cbor=bytes([0xA1,0x61,0x61,0x01]); seg_jumb=box(b'jumb', box(b'jumd', jumd)+box(b'cbor', cbor)); seg=b'JP\\x00\\x00'+(1).to_bytes(4,'big')+seg_jumb; signed_cbor=bytearray(); signed_cbor+=bytes([0xA1,0x68])+b'manifest'; signed_cbor+=bytes([0x81,0xA2,0x6F])+b'claim_generator'; signed_cbor+=bytes([0x64])+b'test'; signed_cbor+=bytes([0x66])+b'claims'; signed_cbor+=bytes([0x81,0xA2,0x6A])+b'assertions'; signed_cbor+=bytes([0x81,0xA1,0x65])+b'label'; signed_cbor+=bytes([0x6E])+b'c2pa.hash.data'; signed_cbor+=bytes([0x6A])+b'signatures'; signed_cbor+=bytes([0x81,0xA2,0x63])+b'alg'; signed_cbor+=bytes([0x65])+b'ES256'; signed_cbor+=bytes([0x69])+b'signature'; signed_cbor+=bytes([0x44,0x01,0x02,0x03,0x04]); signed_jumb=box(b'jumb', box(b'jumd', jumd)+box(b'cbor', bytes(signed_cbor))); Path(r'''${_c2pa_jpg}''').write_bytes(b'\\xFF\\xD8\\xFF\\xEB'+(len(seg)+2).to_bytes(2,'big')+seg+b'\\xFF\\xD9'); Path(r'''${_c2pa_signed_jumb}''').write_bytes(signed_jumb); Path(r'''${_c2pa_manifest}''').write_bytes(bytes(signed_cbor)); Path(r'''${_c2pa_cert}''').write_bytes(bytes([0x30,0x82,0x01,0x00]))"
  RESULT_VARIABLE _rv_c2pa
  OUTPUT_VARIABLE _out_c2pa
  ERROR_VARIABLE _err_c2pa
)
if(NOT _rv_c2pa EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer c2pa fixtures (${_rv_c2pa})\nstdout:\n${_out_c2pa}\nstderr:\n${_err_c2pa}")
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

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --no-exif
          --no-xmp
          --no-icc
          --no-iptc
          --c2pa-policy rewrite
          --dump-c2pa-handoff "${_c2pa_handoff}"
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_handoff
  OUTPUT_VARIABLE _out_handoff
  ERROR_VARIABLE _err_handoff
)
if(NOT _rv_handoff EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer c2pa handoff dump failed (${_rv_handoff})\nstdout:\n${_out_handoff}\nstderr:\n${_err_handoff}")
endif()
if(NOT _out_handoff MATCHES "c2pa_handoff: status=ok")
  message(FATAL_ERROR
    "python metatransfer c2pa handoff dump missing status ok\nstdout:\n${_out_handoff}\nstderr:\n${_err_handoff}")
endif()
if(NOT EXISTS "${_c2pa_handoff}")
  message(FATAL_ERROR
    "python metatransfer c2pa handoff dump did not write output\nstdout:\n${_out_handoff}\nstderr:\n${_err_handoff}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --jpeg-c2pa-signed "${_c2pa_signed_jumb}"
          --c2pa-manifest-output "${_c2pa_manifest}"
          --c2pa-certificate-chain "${_c2pa_cert}"
          --c2pa-key-ref "test-key-ref"
          --c2pa-signing-time "2026-03-09T00:00:00Z"
          --dump-c2pa-signed-package "${_c2pa_signed_package}"
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_signed_package
  OUTPUT_VARIABLE _out_signed_package
  ERROR_VARIABLE _err_signed_package
)
if(NOT _rv_signed_package EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer signed c2pa package dump failed (${_rv_signed_package})\nstdout:\n${_out_signed_package}\nstderr:\n${_err_signed_package}")
endif()
if(NOT _out_signed_package MATCHES "c2pa_signed_package: status=ok")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package dump missing status ok\nstdout:\n${_out_signed_package}\nstderr:\n${_err_signed_package}")
endif()
if(NOT _out_signed_package MATCHES "c2pa_stage_semantics: status=ok reason=ok manifest=1 manifests=1 claim_generator=1 assertions=1 claims=1 signatures=1 linked=1 orphan=0 explicit_refs=0 unresolved=0 ambiguous=0")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package dump missing semantic summary\nstdout:\n${_out_signed_package}\nstderr:\n${_err_signed_package}")
endif()
if(NOT _out_signed_package MATCHES "c2pa_stage_linkage: claim0_assertions=1 claim0_refs=1 sig0_links=1")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package dump missing linkage summary\nstdout:\n${_out_signed_package}\nstderr:\n${_err_signed_package}")
endif()
if(NOT EXISTS "${_c2pa_signed_package}")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package dump did not write output\nstdout:\n${_out_signed_package}\nstderr:\n${_err_signed_package}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --load-c2pa-signed-package "${_c2pa_signed_package}"
          --target-jpeg "${_c2pa_jpg}"
          -o "${_c2pa_from_package}"
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_from_package
  OUTPUT_VARIABLE _out_from_package
  ERROR_VARIABLE _err_from_package
)
if(NOT _rv_from_package EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer signed c2pa package apply failed (${_rv_from_package})\nstdout:\n${_out_from_package}\nstderr:\n${_err_from_package}")
endif()
if(NOT _out_from_package MATCHES "c2pa_stage: status=ok")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package apply missing stage ok\nstdout:\n${_out_from_package}\nstderr:\n${_err_from_package}")
endif()
if(NOT _out_from_package MATCHES "c2pa_stage_semantics: status=ok reason=ok manifest=1 manifests=1 claim_generator=1 assertions=1 claims=1 signatures=1 linked=1 orphan=0 explicit_refs=0 unresolved=0 ambiguous=0")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package apply missing semantic summary\nstdout:\n${_out_from_package}\nstderr:\n${_err_from_package}")
endif()
if(NOT _out_from_package MATCHES "c2pa_stage_linkage: claim0_assertions=1 claim0_refs=1 sig0_links=1")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package apply missing linkage summary\nstdout:\n${_out_from_package}\nstderr:\n${_err_from_package}")
endif()
if(NOT _out_from_package MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package apply missing edit ok\nstdout:\n${_out_from_package}\nstderr:\n${_err_from_package}")
endif()
if(NOT EXISTS "${_c2pa_from_package}")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package apply did not write output\nstdout:\n${_out_from_package}\nstderr:\n${_err_from_package}")
endif()

message(STATUS "python metatransfer edit smoke gate passed")
