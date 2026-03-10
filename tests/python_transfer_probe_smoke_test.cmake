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

signed_cbor = bytearray()
signed_cbor += bytes([0xA1])
signed_cbor += bytes([0x68]) + b'manifest'
signed_cbor += bytes([0x81])
signed_cbor += bytes([0xA2])
signed_cbor += bytes([0x6F]) + b'claim_generator'
signed_cbor += bytes([0x64]) + b'test'
signed_cbor += bytes([0x66]) + b'claims'
signed_cbor += bytes([0x81])
signed_cbor += bytes([0xA2])
signed_cbor += bytes([0x6A]) + b'assertions'
signed_cbor += bytes([0x81])
signed_cbor += bytes([0xA1])
signed_cbor += bytes([0x65]) + b'label'
signed_cbor += bytes([0x6E]) + b'c2pa.hash.data'
signed_cbor += bytes([0x6A]) + b'signatures'
signed_cbor += bytes([0x81])
signed_cbor += bytes([0xA2])
signed_cbor += bytes([0x63]) + b'alg'
signed_cbor += bytes([0x65]) + b'ES256'
signed_cbor += bytes([0x69]) + b'signature'
signed_cbor += bytes([0x44, 0x01, 0x02, 0x03, 0x04])
signed_jumb = box(b'jumb', box(b'jumd', jumd) + box(b'cbor', bytes(signed_cbor)))

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
assert r4['c2pa_sign_request']['status_name'] == 'unsupported', r4
assert r4['c2pa_sign_request']['carrier_route'] == 'jpeg:app11-c2pa', r4

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
assert r5['c2pa_sign_request']['status_name'] == 'ok', r5
assert r5['c2pa_sign_request']['carrier_route'] == 'jpeg:app11-c2pa', r5
assert r5['c2pa_sign_request']['manifest_label'] == 'c2pa', r5
assert r5['c2pa_sign_request']['source_range_chunks'] == 2, r5
assert r5['c2pa_sign_request']['prepared_segment_chunks'] == 0, r5
assert r5['c2pa_sign_request']['content_binding_bytes'] == 4, r5

r5b = openmeta.unsafe_transfer_probe(
    str(p_c2pa),
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_policy=openmeta.TransferPolicyAction.Rewrite,
    include_c2pa_binding_bytes=True,
)
assert r5b['c2pa_binding_requested'] is True, r5b
assert r5b['c2pa_binding_status_name'] == 'ok', r5b
assert r5b['c2pa_binding_code_name'] == 'none', r5b
assert r5b['c2pa_binding_bytes_written'] == 4, r5b
assert isinstance(r5b['c2pa_binding_bytes'], (bytes, bytearray)), r5b
assert bytes(r5b['c2pa_binding_bytes']) == bytes([0xFF, 0xD8, 0xFF, 0xD9]), r5b
assert r5b['c2pa_handoff_requested'] is False, r5b

r5c = openmeta.unsafe_transfer_probe(
    str(p_c2pa),
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_policy=openmeta.TransferPolicyAction.Rewrite,
    include_c2pa_handoff_bytes=True,
)
assert r5c['c2pa_handoff_requested'] is True, r5c
assert r5c['c2pa_handoff_status_name'] == 'ok', r5c
assert r5c['c2pa_handoff_code_name'] == 'none', r5c
assert r5c['c2pa_handoff_bytes_written'] > 0, r5c
assert isinstance(r5c['c2pa_handoff_bytes'], (bytes, bytearray)), r5c

r6 = openmeta.unsafe_transfer_probe(
    str(p_c2pa),
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_signed_logical_payload=signed_jumb,
    c2pa_certificate_chain=bytes([0x30, 0x82, 0x01, 0x00]),
    c2pa_private_key_reference='test-key-ref',
    c2pa_signing_time='2026-03-09T00:00:00Z',
    c2pa_manifest_builder_output=bytes(signed_cbor),
    edit_target_path=str(p_c2pa),
    edit_apply=True,
    include_edited_bytes=True,
)
assert r6['prepare_status'] == openmeta.TransferStatus.Unsupported, r6
assert r6['c2pa_stage_requested'] is True, r6
assert r6['c2pa_stage_validation_status_name'] == 'ok', r6
assert r6['c2pa_stage_validation_code_name'] == 'none', r6
assert r6['c2pa_stage_validation_payload_kind_name'] == 'content_bound', r6
assert r6['c2pa_stage_validation_semantic_status_name'] == 'ok', r6
assert r6['c2pa_stage_validation_semantic_reason'] == 'ok', r6
assert r6['c2pa_stage_validation_semantic_manifest_present'] == 1, r6
assert r6['c2pa_stage_validation_semantic_manifest_count'] == 1, r6
assert r6['c2pa_stage_validation_semantic_claim_generator_present'] == 1, r6
assert r6['c2pa_stage_validation_semantic_assertion_count'] == 1, r6
assert r6['c2pa_stage_validation_semantic_primary_claim_assertion_count'] == 1, r6
assert r6['c2pa_stage_validation_semantic_primary_claim_referenced_by_signature_count'] == 1, r6
assert r6['c2pa_stage_validation_semantic_primary_signature_linked_claim_count'] == 1, r6
assert r6['c2pa_stage_validation_semantic_primary_signature_reference_key_hits'] == 0, r6
assert r6['c2pa_stage_validation_semantic_primary_signature_explicit_reference_present'] == 0, r6
assert r6['c2pa_stage_validation_semantic_primary_signature_explicit_reference_resolved_claim_count'] == 0, r6
assert r6['c2pa_stage_validation_semantic_claim_count'] == 1, r6
assert r6['c2pa_stage_validation_semantic_signature_count'] == 1, r6
assert r6['c2pa_stage_validation_semantic_signature_linked'] == 1, r6
assert r6['c2pa_stage_validation_semantic_signature_orphan'] == 0, r6
assert r6['c2pa_stage_validation_staged_segments'] == 1, r6
assert r6['c2pa_stage_status_name'] == 'ok', r6
assert any(d['subject_name'] == 'c2pa' and d['reason_name'] == 'external_signed_payload' and d['c2pa_mode_name'] == 'signed_rewrite' and d['c2pa_prepared_output_name'] == 'signed_rewrite' for d in r6['policy_decisions']), r6
assert r6['c2pa_rewrite']['state_name'] == 'ready', r6
assert r6['c2pa_sign_request']['status_name'] == 'ok', r6
assert r6['overall_status'] == openmeta.TransferStatus.Ok, r6
assert r6['edit_apply_status'] == openmeta.TransferStatus.Ok, r6
assert isinstance(r6['edited_bytes'], (bytes, bytearray)), r6

r6b = openmeta.unsafe_transfer_probe(
    str(p_c2pa),
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_signed_logical_payload=signed_jumb,
    c2pa_certificate_chain=bytes([0x30, 0x82, 0x01, 0x00]),
    c2pa_private_key_reference='test-key-ref',
    c2pa_signing_time='2026-03-09T00:00:00Z',
    c2pa_manifest_builder_output=bytes(signed_cbor),
    include_c2pa_signed_package_bytes=True,
)
assert r6b['c2pa_signed_package_requested'] is True, r6b
assert r6b['c2pa_signed_package_status_name'] == 'ok', r6b
assert r6b['c2pa_signed_package_code_name'] == 'none', r6b
assert r6b['c2pa_signed_package_bytes_written'] > 0, r6b
assert isinstance(r6b['c2pa_signed_package_bytes'], (bytes, bytearray)), r6b

r7 = openmeta.unsafe_transfer_probe(
    str(p_c2pa),
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_signed_package=bytes(r6b['c2pa_signed_package_bytes']),
    edit_target_path=str(p_c2pa),
    edit_apply=True,
    include_edited_bytes=True,
)
assert r7['c2pa_stage_requested'] is True, r7
assert r7['c2pa_stage_validation_status_name'] == 'ok', r7
assert r7['c2pa_stage_validation_semantic_status_name'] == 'ok', r7
assert r7['c2pa_stage_validation_semantic_manifest_count'] == 1, r7
assert r7['c2pa_stage_validation_semantic_claim_generator_present'] == 1, r7
assert r7['c2pa_stage_validation_semantic_assertion_count'] == 1, r7
assert r7['c2pa_stage_validation_semantic_primary_claim_assertion_count'] == 1, r7
assert r7['c2pa_stage_validation_semantic_primary_claim_referenced_by_signature_count'] == 1, r7
assert r7['c2pa_stage_validation_semantic_primary_signature_linked_claim_count'] == 1, r7
assert r7['c2pa_stage_validation_semantic_primary_signature_reference_key_hits'] == 0, r7
assert r7['c2pa_stage_validation_semantic_primary_signature_explicit_reference_present'] == 0, r7
assert r7['c2pa_stage_validation_semantic_primary_signature_explicit_reference_resolved_claim_count'] == 0, r7
assert r7['c2pa_stage_status_name'] == 'ok', r7
assert r7['overall_status'] == openmeta.TransferStatus.Ok, r7
assert isinstance(r7['edited_bytes'], (bytes, bytearray)), r7
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
