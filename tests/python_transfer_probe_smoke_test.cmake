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

p_c2pa = Path(r'''${WORK_DIR}/sample_c2pa.jpg''')
cbor = bytes([0xA1, 0x61, 0x61, 0x01])
jumd = b'c2pa\\x00'
box = lambda t, payload: (8 + len(payload)).to_bytes(4, 'big') + t + payload
jumb = box(b'jumb', box(b'jumd', jumd) + box(b'cbor', cbor))
seg = b'JP\\x00\\x00' + (1).to_bytes(4, 'big') + jumb
p_c2pa.write_bytes(b'\\xFF\\xD8\\xFF\\xEB' + (len(seg) + 2).to_bytes(2, 'big') + seg + b'\\xFF\\xD9')

r4 = openmeta.transfer_probe(
    str(p_c2pa),
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_policy=openmeta.TransferPolicyAction.Invalidate,
)
assert r4['prepare_status'] == openmeta.TransferStatus.Ok, r4
assert any(d['subject_name'] == 'c2pa' and d['effective_name'] == 'keep' and d['reason_name'] == 'draft_invalidation_payload' and d['c2pa_mode_name'] == 'draft_unsigned_invalidation' and d['c2pa_source_kind_name'] == 'content_bound' and d['c2pa_prepared_output_name'] == 'generated_draft_unsigned_invalidation' for d in r4['policy_decisions']), r4
assert any(b['route'] == 'jpeg:app11-c2pa' for b in r4['blocks']), r4
assert r4['c2pa_rewrite']['state_name'] == 'not_requested', r4
assert r4['c2pa_rewrite']['matched_entries'] > 0, r4
assert r4['c2pa_rewrite']['existing_carrier_segments'] == 1, r4

r5 = openmeta.transfer_probe(
    str(p_c2pa),
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_policy=openmeta.TransferPolicyAction.Rewrite,
)
assert r5['prepare_status'] == openmeta.TransferStatus.Unsupported, r5
assert any(d['subject_name'] == 'c2pa' and d['reason_name'] == 'signed_rewrite_unavailable' for d in r5['policy_decisions']), r5
assert r5['c2pa_rewrite']['state_name'] == 'signing_material_required', r5
assert r5['c2pa_rewrite']['source_kind_name'] == 'content_bound', r5
assert r5['c2pa_rewrite']['matched_entries'] > 0, r5
assert r5['c2pa_rewrite']['existing_carrier_segments'] == 1, r5
assert r5['c2pa_rewrite']['target_carrier_available'] is True, r5
assert r5['c2pa_rewrite']['requires_private_key'] is True, r5
assert r5['c2pa_rewrite']['content_binding_bytes'] == 4, r5
assert len(r5['c2pa_rewrite']['content_binding_chunks']) == 2, r5
assert all(c['kind_name'] == 'source_range' for c in r5['c2pa_rewrite']['content_binding_chunks']), r5
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
