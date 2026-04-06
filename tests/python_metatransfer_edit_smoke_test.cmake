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
set(_src_jpg_xmp "${WORK_DIR}/source_xmp.jpg")
set(_src_icc_jpg "${WORK_DIR}/source_icc.jpg")
set(_target_jpg "${WORK_DIR}/target.jpg")
set(_target_jpg_xmp "${WORK_DIR}/target_xmp.jpg")
set(_edited_jpg "${WORK_DIR}/edited.jpg")
set(_dual_jpg "${WORK_DIR}/dual_write.jpg")
set(_dual_jpg_sidecar "${WORK_DIR}/dual_write.xmp")
set(_embed_only_strip_jpg "${WORK_DIR}/embed_only_strip.jpg")
set(_embed_only_strip_sidecar "${WORK_DIR}/embed_only_strip.xmp")
set(_sidecar_only_strip_jpg "${WORK_DIR}/sidecar_only_strip.jpg")
set(_sidecar_only_strip_sidecar "${WORK_DIR}/sidecar_only_strip.xmp")
set(_destination_merge_jpg "${WORK_DIR}/destination_merge.jpg")
set(_target_tif "${WORK_DIR}/target.tif")
set(_target_dng "${WORK_DIR}/target.dng")
set(_target_tif_xmp "${WORK_DIR}/target_xmp.tif")
set(_edited_tif "${WORK_DIR}/edited.tif")
set(_edited_dng "${WORK_DIR}/edited.dng")
set(_sidecar_only_strip_tif "${WORK_DIR}/sidecar_only_strip_tiff.tif")
set(_sidecar_only_strip_tif_sidecar "${WORK_DIR}/sidecar_only_strip_tiff.xmp")
set(_target_jxl "${WORK_DIR}/target.jxl")
set(_edited_jxl "${WORK_DIR}/edited.jxl")
set(_target_jp2 "${WORK_DIR}/target.jp2")
set(_edited_jp2 "${WORK_DIR}/edited.jp2")
set(_jxl_handoff "${WORK_DIR}/jxl_encoder_handoff.omjxic")
set(_c2pa_jpg "${WORK_DIR}/sample_c2pa.jpg")
set(_c2pa_jxl "${WORK_DIR}/sample_c2pa.jxl")
set(_c2pa_heif "${WORK_DIR}/sample_c2pa.heif")
set(_c2pa_signed_jumb "${WORK_DIR}/signed_c2pa.jumb")
set(_c2pa_manifest "${WORK_DIR}/manifest.cbor")
set(_c2pa_cert "${WORK_DIR}/cert.der")
set(_c2pa_handoff "${WORK_DIR}/handoff.omc2ph")
set(_c2pa_heif_handoff "${WORK_DIR}/heif_handoff.omc2ph")
set(_c2pa_signed_package "${WORK_DIR}/signed_package.omc2ps")
set(_c2pa_jxl_signed_package "${WORK_DIR}/jxl_signed_package.omc2ps")
set(_c2pa_heif_signed_package "${WORK_DIR}/heif_signed_package.omc2ps")
set(_c2pa_jxl_binding "${WORK_DIR}/jxl_binding.bin")
set(_c2pa_heif_binding "${WORK_DIR}/heif_binding.bin")
set(_transfer_payload_batch "${WORK_DIR}/payload_batch.omtpld")
set(_transfer_package_batch "${WORK_DIR}/package_batch.omtpkg")
set(_c2pa_from_package "${WORK_DIR}/edited_from_package.jpg")
set(_c2pa_jxl_out "${WORK_DIR}/edited_from_package.jxl")
set(_c2pa_heif_out "${WORK_DIR}/edited_from_package.heif")
set(_c2pa_heif_from_package "${WORK_DIR}/edited_from_signed_package.heif")
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
    "from pathlib import Path; t=bytearray(); t+=b'II*\\x00'; t+=(8).to_bytes(4,'little'); t+=(1).to_bytes(2,'little'); t+=(0x0132).to_bytes(2,'little'); t+=(2).to_bytes(2,'little'); t+=(20).to_bytes(4,'little'); t+=(26).to_bytes(4,'little'); t+=(0).to_bytes(4,'little'); t+=b'2000:01:02 03:04:05\\x00'; app1=b'Exif\\x00\\x00'+bytes(t); xml=b\"<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'><xmp:CreatorTool>OpenMeta Transfer Source</xmp:CreatorTool></rdf:Description></rdf:RDF></x:xmpmeta>\"; xmp=b'http://ns.adobe.com/xap/1.0/\\x00'+xml; ln1=(len(app1)+2).to_bytes(2,'big'); ln2=(len(xmp)+2).to_bytes(2,'big'); Path(r'''${_src_jpg_xmp}''').write_bytes(b'\\xFF\\xD8\\xFF\\xE1'+ln1+app1+b'\\xFF\\xE1'+ln2+xmp+b'\\xFF\\xD9')"
  RESULT_VARIABLE _rv_src_xmp
  OUTPUT_VARIABLE _out_src_xmp
  ERROR_VARIABLE _err_src_xmp
)
if(NOT _rv_src_xmp EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer xmp source fixture (${_rv_src_xmp})\nstdout:\n${_out_src_xmp}\nstderr:\n${_err_src_xmp}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; p=bytearray(156); p[0:4]=(156).to_bytes(4,'big'); p[36:40]=b'acsp'; p[128:132]=(1).to_bytes(4,'big'); p[132:136]=b'desc'; p[136:140]=(144).to_bytes(4,'big'); p[140:144]=(12).to_bytes(4,'big'); p[144:156]=bytes([0x11])*12; app2=b'ICC_PROFILE\\x00\\x01\\x01'+bytes(p); ln=(len(app2)+2).to_bytes(2,'big'); Path(r'''${_src_icc_jpg}''').write_bytes(b'\\xFF\\xD8\\xFF\\xE2'+ln+app2+b'\\xFF\\xD9')"
  RESULT_VARIABLE _rv_src_icc
  OUTPUT_VARIABLE _out_src_icc
  ERROR_VARIABLE _err_src_icc
)
if(NOT _rv_src_icc EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer icc source fixture (${_rv_src_icc})\nstdout:\n${_out_src_icc}\nstderr:\n${_err_src_icc}")
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
    "from pathlib import Path; xml=b\"<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'><xmp:CreatorTool>Target Embedded Existing</xmp:CreatorTool></rdf:Description></rdf:RDF></x:xmpmeta>\"; app1=b'http://ns.adobe.com/xap/1.0/\\x00'+xml; ln=(len(app1)+2).to_bytes(2,'big'); Path(r'''${_target_jpg_xmp}''').write_bytes(b'\\xFF\\xD8\\xFF\\xE1'+ln+app1+b'\\xFF\\xD9')"
  RESULT_VARIABLE _rv_target_jpg_xmp
  OUTPUT_VARIABLE _out_target_jpg_xmp
  ERROR_VARIABLE _err_target_jpg_xmp
)
if(NOT _rv_target_jpg_xmp EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target jpeg xmp fixture (${_rv_target_jpg_xmp})\nstdout:\n${_out_target_jpg_xmp}\nstderr:\n${_err_target_jpg_xmp}")
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
    "from pathlib import Path; b=bytearray(); b+=b'II'; b+=(42).to_bytes(2,'little'); b+=(8).to_bytes(4,'little'); b+=(0).to_bytes(2,'little'); b+=(0).to_bytes(4,'little'); Path(r'''${_target_dng}''').write_bytes(bytes(b))"
  RESULT_VARIABLE _rv_target_dng
  OUTPUT_VARIABLE _out_target_dng
  ERROR_VARIABLE _err_target_dng
)
if(NOT _rv_target_dng EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target dng fixture (${_rv_target_dng})\nstdout:\n${_out_target_dng}\nstderr:\n${_err_target_dng}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; xml=b\"<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'><xmp:CreatorTool>Target Embedded Existing</xmp:CreatorTool></rdf:Description></rdf:RDF></x:xmpmeta>\"; xoff=38; ifd=bytearray(); ifd+=(1).to_bytes(2,'little'); ifd+=(700).to_bytes(2,'little'); ifd+=(1).to_bytes(2,'little'); ifd+=len(xml).to_bytes(4,'little'); ifd+=xoff.to_bytes(4,'little'); ifd+=(0).to_bytes(4,'little'); b=bytearray(); b+=b'II'; b+=(42).to_bytes(2,'little'); b+=(8).to_bytes(4,'little'); b+=ifd; b+=xml; Path(r'''${_target_tif_xmp}''').write_bytes(bytes(b))"
  RESULT_VARIABLE _rv_target_tif_xmp
  OUTPUT_VARIABLE _out_target_tif_xmp
  ERROR_VARIABLE _err_target_tif_xmp
)
if(NOT _rv_target_tif_xmp EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target tiff xmp fixture (${_rv_target_tif_xmp})\nstdout:\n${_out_target_tif_xmp}\nstderr:\n${_err_target_tif_xmp}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; u32=lambda v:(v).to_bytes(4,'big'); box=lambda t,p:u32(8+len(p))+t+p; Path(r'''${_target_jxl}''').write_bytes(u32(12)+b'JXL '+u32(0x0D0A870A)+box(b'jxlc', bytes([0x11,0x22,0x33,0x44])))"
  RESULT_VARIABLE _rv_target_jxl
  OUTPUT_VARIABLE _out_target_jxl
  ERROR_VARIABLE _err_target_jxl
)
if(NOT _rv_target_jxl EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target jxl fixture (${_rv_target_jxl})\nstdout:\n${_out_target_jxl}\nstderr:\n${_err_target_jxl}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; u32=lambda v:(v).to_bytes(4,'big'); box=lambda t,p:u32(8+len(p))+t+p; ftyp=b'jp2 '+u32(0)+b'jp2 '; sig=u32(12)+b'jP  '+u32(0x0D0A870A); Path(r'''${_target_jp2}''').write_bytes(sig+box(b'ftyp', ftyp)+box(b'free', bytes([0x11,0x22,0x33])))"
  RESULT_VARIABLE _rv_target_jp2
  OUTPUT_VARIABLE _out_target_jp2
  ERROR_VARIABLE _err_target_jp2
)
if(NOT _rv_target_jp2 EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target jp2 fixture (${_rv_target_jp2})\nstdout:\n${_out_target_jp2}\nstderr:\n${_err_target_jp2}")
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
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; u32=lambda v:(v).to_bytes(4,'big'); box=lambda t,p:u32(8+len(p))+t+p; cbor=bytes([0xA1,0x61,0x61,0x01]); jumd=b'c2pa\\x00'; logical=box(b'jumb', box(b'jumd', jumd)+box(b'cbor', cbor)); Path(r'''${_c2pa_jxl}''').write_bytes(u32(12)+b'JXL '+u32(0x0D0A870A)+box(b'jxlc', bytes([0x11,0x22,0x33,0x44]))+box(b'jumb', logical[8:]))"
  RESULT_VARIABLE _rv_c2pa_jxl
  OUTPUT_VARIABLE _out_c2pa_jxl
  ERROR_VARIABLE _err_c2pa_jxl
)
if(NOT _rv_c2pa_jxl EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer jxl c2pa fixture (${_rv_c2pa_jxl})\nstdout:\n${_out_c2pa_jxl}\nstderr:\n${_err_c2pa_jxl}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; u32=lambda v:(v).to_bytes(4,'big'); box=lambda t,p:u32(8+len(p))+t+p; uuidbox=lambda u,p:u32(24+len(p))+b'uuid'+u+p; logical=Path(r'''${_c2pa_signed_jumb}''').read_bytes(); marker=b'openmeta:bmff_transfer_meta:v1'; uuid=b'OpenMetaBmffMeta'; infe=b'\\x02\\x00\\x00\\x00'+(1).to_bytes(2,'big')+(0).to_bytes(2,'big')+b'c2pa'+b'C2PA\\x00'; iinf=box(b'iinf', b'\\x00\\x00\\x00\\x00'+(1).to_bytes(2,'big')+box(b'infe', infe)); idat=box(b'idat', logical); iloc=box(b'iloc', b'\\x01\\x00\\x00\\x00'+bytes([0x44,0x40])+(1).to_bytes(2,'big')+(1).to_bytes(2,'big')+(1).to_bytes(2,'big')+(0).to_bytes(2,'big')+(0).to_bytes(4,'big')+(1).to_bytes(2,'big')+(0).to_bytes(4,'big')+len(logical).to_bytes(4,'big')); meta=box(b'meta', b'\\x00\\x00\\x00\\x00'+uuidbox(uuid, marker)+iinf+idat+iloc); ftyp=box(b'ftyp', b'heic'+u32(0)+b'mif1heic'); mdat=box(b'mdat', bytes([0x11,0x22,0x33,0x44])); Path(r'''${_c2pa_heif}''').write_bytes(ftyp+mdat+meta)"
  RESULT_VARIABLE _rv_c2pa_heif
  OUTPUT_VARIABLE _out_c2pa_heif
  ERROR_VARIABLE _err_c2pa_heif
)
if(NOT _rv_c2pa_heif EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer heif c2pa fixture (${_rv_c2pa_heif})\nstdout:\n${_out_c2pa_heif}\nstderr:\n${_err_c2pa_heif}")
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

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jpeg "${_target_jpg}"
          --xmp-writeback embedded_and_sidecar
          -o "${_dual_jpg}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_dual
  OUTPUT_VARIABLE _out_dual
  ERROR_VARIABLE _err_dual
)
if(NOT _rv_dual EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer dual-write jpeg edit failed (${_rv_dual})\nstdout:\n${_out_dual}\nstderr:\n${_err_dual}")
endif()
if(NOT EXISTS "${_dual_jpg}")
  message(FATAL_ERROR
    "python metatransfer dual-write did not write jpeg output\nstdout:\n${_out_dual}\nstderr:\n${_err_dual}")
endif()
if(NOT EXISTS "${_dual_jpg_sidecar}")
  message(FATAL_ERROR
    "python metatransfer dual-write did not write xmp sidecar\nstdout:\n${_out_dual}\nstderr:\n${_err_dual}")
endif()
if(NOT _out_dual MATCHES "xmp_sidecar_output=.*dual_write\\.xmp")
  message(FATAL_ERROR
    "python metatransfer dual-write missing xmp sidecar summary\nstdout:\n${_out_dual}\nstderr:\n${_err_dual}")
endif()
execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; b=Path(r'''${_dual_jpg_sidecar}''').read_bytes(); import sys; sys.exit(0 if (b.find(b'<x:xmpmeta')!=-1 or b.find(b'<rdf:RDF')!=-1) else 1)"
  RESULT_VARIABLE _rv_dual_check
  OUTPUT_VARIABLE _out_dual_check
  ERROR_VARIABLE _err_dual_check
)
if(NOT _rv_dual_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer dual-write sidecar content check failed (${_rv_dual_check})\nstdout:\n${_out_dual_check}\nstderr:\n${_err_dual_check}")
endif()

file(WRITE "${_embed_only_strip_sidecar}" "stale sidecar\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jpeg "${_target_jpg}"
          --xmp-destination-sidecar strip_existing
          -o "${_embed_only_strip_jpg}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_embed_strip
  OUTPUT_VARIABLE _out_embed_strip
  ERROR_VARIABLE _err_embed_strip
)
if(NOT _rv_embed_strip EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer embedded-only sidecar cleanup failed (${_rv_embed_strip})\nstdout:\n${_out_embed_strip}\nstderr:\n${_err_embed_strip}")
endif()
if(NOT EXISTS "${_embed_only_strip_jpg}")
  message(FATAL_ERROR
    "python metatransfer embedded-only cleanup did not write jpeg output\nstdout:\n${_out_embed_strip}\nstderr:\n${_err_embed_strip}")
endif()
if(EXISTS "${_embed_only_strip_sidecar}")
  message(FATAL_ERROR
    "python metatransfer embedded-only cleanup did not remove stale sidecar\nstdout:\n${_out_embed_strip}\nstderr:\n${_err_embed_strip}")
endif()
if(NOT _out_embed_strip MATCHES "xmp_sidecar_removed=.*embed_only_strip\\.xmp")
  message(FATAL_ERROR
    "python metatransfer embedded-only cleanup missing sidecar removal summary\nstdout:\n${_out_embed_strip}\nstderr:\n${_err_embed_strip}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jpeg "${_target_jpg_xmp}"
          --xmp-writeback sidecar
          --xmp-destination-embedded strip_existing
          -o "${_sidecar_only_strip_jpg}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_sidecar_strip
  OUTPUT_VARIABLE _out_sidecar_strip
  ERROR_VARIABLE _err_sidecar_strip
)
if(NOT _rv_sidecar_strip EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer sidecar-only embedded-strip failed (${_rv_sidecar_strip})\nstdout:\n${_out_sidecar_strip}\nstderr:\n${_err_sidecar_strip}")
endif()
if(NOT EXISTS "${_sidecar_only_strip_jpg}")
  message(FATAL_ERROR
    "python metatransfer sidecar-only embedded-strip did not write jpeg output\nstdout:\n${_out_sidecar_strip}\nstderr:\n${_err_sidecar_strip}")
endif()
if(NOT EXISTS "${_sidecar_only_strip_sidecar}")
  message(FATAL_ERROR
    "python metatransfer sidecar-only embedded-strip did not write xmp sidecar\nstdout:\n${_out_sidecar_strip}\nstderr:\n${_err_sidecar_strip}")
endif()
execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; b=Path(r'''${_sidecar_only_strip_jpg}''').read_bytes(); import sys; sys.exit(0 if (b.find(b'Target Embedded Existing')==-1 and b.find(b'http://ns.adobe.com/xap/1.0/')==-1) else 1)"
  RESULT_VARIABLE _rv_sidecar_strip_check
  OUTPUT_VARIABLE _out_sidecar_strip_check
  ERROR_VARIABLE _err_sidecar_strip_check
)
if(NOT _rv_sidecar_strip_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer sidecar-only embedded-strip output still contains embedded xmp (${_rv_sidecar_strip_check})\nstdout:\n${_out_sidecar_strip}\nstderr:\n${_err_sidecar_strip}\ncheck_stderr:\n${_err_sidecar_strip_check}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-tiff "${_target_tif_xmp}"
          --xmp-writeback sidecar
          --xmp-destination-embedded strip_existing
          -o "${_sidecar_only_strip_tif}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_sidecar_strip_tif
  OUTPUT_VARIABLE _out_sidecar_strip_tif
  ERROR_VARIABLE _err_sidecar_strip_tif
)
if(NOT _rv_sidecar_strip_tif EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer tiff sidecar-only embedded-strip failed (${_rv_sidecar_strip_tif})\nstdout:\n${_out_sidecar_strip_tif}\nstderr:\n${_err_sidecar_strip_tif}")
endif()
if(NOT EXISTS "${_sidecar_only_strip_tif}")
  message(FATAL_ERROR
    "python metatransfer tiff sidecar-only embedded-strip did not write output\nstdout:\n${_out_sidecar_strip_tif}\nstderr:\n${_err_sidecar_strip_tif}")
endif()
if(NOT EXISTS "${_sidecar_only_strip_tif_sidecar}")
  message(FATAL_ERROR
    "python metatransfer tiff sidecar-only embedded-strip did not write xmp sidecar\nstdout:\n${_out_sidecar_strip_tif}\nstderr:\n${_err_sidecar_strip_tif}")
endif()
execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; import sys; b=Path(r'''${_sidecar_only_strip_tif}''').read_bytes(); ok=(len(b)>=8 and b[0:2]==b'II' and int.from_bytes(b[2:4],'little')==42); off=int.from_bytes(b[4:8],'little') if ok else 0; ok=ok and (off+2<=len(b)); n=int.from_bytes(b[off:off+2],'little') if ok else 0; p=off+2; ok=ok and (p+n*12+4<=len(b)); tags=[int.from_bytes(b[p+i*12:p+i*12+2],'little') for i in range(n)] if ok else []; sys.exit(0 if (ok and 700 not in tags and 0x0132 in tags) else 1)"
  RESULT_VARIABLE _rv_sidecar_strip_tif_check
  OUTPUT_VARIABLE _out_sidecar_strip_tif_check
  ERROR_VARIABLE _err_sidecar_strip_tif_check
)
if(NOT _rv_sidecar_strip_tif_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer tiff sidecar-only embedded-strip output still contains xmp tag 700 or lost DateTime (${_rv_sidecar_strip_tif_check})\nstdout:\n${_out_sidecar_strip_tif}\nstderr:\n${_err_sidecar_strip_tif}\ncheck_stderr:\n${_err_sidecar_strip_tif_check}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jpeg "${_target_jpg_xmp}"
          --xmp-include-existing
          --xmp-conflict-policy existing_wins
          --xmp-include-existing-destination-embedded
          --xmp-existing-destination-embedded-precedence source_wins
          -o "${_destination_merge_jpg}"
          "${_src_jpg_xmp}"
  RESULT_VARIABLE _rv_destination_merge
  OUTPUT_VARIABLE _out_destination_merge
  ERROR_VARIABLE _err_destination_merge
)
if(NOT _rv_destination_merge EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer destination embedded merge failed (${_rv_destination_merge})\nstdout:\n${_out_destination_merge}\nstderr:\n${_err_destination_merge}")
endif()
if(NOT EXISTS "${_destination_merge_jpg}")
  message(FATAL_ERROR
    "python metatransfer destination embedded merge did not write jpeg output\nstdout:\n${_out_destination_merge}\nstderr:\n${_err_destination_merge}")
endif()
if(NOT _out_destination_merge MATCHES "xmp_existing_destination_embedded: status=ok loaded=yes path=.*target_xmp\\.jpg")
  message(FATAL_ERROR
    "python metatransfer destination embedded merge missing status summary\nstdout:\n${_out_destination_merge}\nstderr:\n${_err_destination_merge}")
endif()
execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; b=Path(r'''${_destination_merge_jpg}''').read_bytes(); import sys; sys.exit(0 if (b.find(b'OpenMeta Transfer Source')!=-1 and b.find(b'Target Embedded Existing')==-1) else 1)"
  RESULT_VARIABLE _rv_destination_merge_check
  OUTPUT_VARIABLE _out_destination_merge_check
  ERROR_VARIABLE _err_destination_merge_check
)
if(NOT _rv_destination_merge_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer destination embedded precedence check failed (${_rv_destination_merge_check})\nstdout:\n${_out_destination_merge}\nstderr:\n${_err_destination_merge}\ncheck_stderr:\n${_err_destination_merge_check}")
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
          --time-patch "DateTime=2024:12:31 23:59:59"
          --target-dng "${_target_dng}"
          -o "${_edited_dng}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_dng
  OUTPUT_VARIABLE _out_dng
  ERROR_VARIABLE _err_dng
)
if(NOT _rv_dng EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer dng edit failed (${_rv_dng})\nstdout:\n${_out_dng}\nstderr:\n${_err_dng}")
endif()
if(NOT _out_dng MATCHES "edit_plan: status=ok")
  message(FATAL_ERROR
    "python metatransfer dng edit missing plan ok\nstdout:\n${_out_dng}\nstderr:\n${_err_dng}")
endif()
if(NOT _out_dng MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer dng edit missing apply ok\nstdout:\n${_out_dng}\nstderr:\n${_err_dng}")
endif()
if(NOT EXISTS "${_edited_dng}")
  message(FATAL_ERROR
    "python metatransfer dng edit did not write output\nstdout:\n${_out_dng}\nstderr:\n${_err_dng}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_tiff_py}" "${_edited_dng}" "2024:12:31 23:59:59"
  RESULT_VARIABLE _rv_dng_check
  OUTPUT_VARIABLE _out_dng_check
  ERROR_VARIABLE _err_dng_check
)
if(NOT _rv_dng_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer dng output check failed (${_rv_dng_check})\nstdout:\n${_out_dng_check}\nstderr:\n${_err_dng_check}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jxl
          --no-xmp
          --no-icc
          --no-iptc
          "${_src_jpg}"
  RESULT_VARIABLE _rv_jxl
  OUTPUT_VARIABLE _out_jxl
  ERROR_VARIABLE _err_jxl
)
if(NOT _rv_jxl EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl summary failed (${_rv_jxl})\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()
if(NOT _out_jxl MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl summary missing compile ok\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()
if(NOT _out_jxl MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl summary missing emit ok\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()
if(NOT _out_jxl MATCHES "jxl_box Exif count=1")
  message(FATAL_ERROR
    "python metatransfer jxl summary missing Exif box summary\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jxl
          --no-exif
          --no-xmp
          --no-iptc
          "${_src_icc_jpg}"
  RESULT_VARIABLE _rv_jxl_icc
  OUTPUT_VARIABLE _out_jxl_icc
  ERROR_VARIABLE _err_jxl_icc
)
if(NOT _rv_jxl_icc EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl icc summary failed (${_rv_jxl_icc})\nstdout:\n${_out_jxl_icc}\nstderr:\n${_err_jxl_icc}")
endif()
if(NOT _out_jxl_icc MATCHES "jxl_icc_profile bytes=[1-9][0-9]*")
  message(FATAL_ERROR
    "python metatransfer jxl icc summary missing encoder icc handoff\nstdout:\n${_out_jxl_icc}\nstderr:\n${_err_jxl_icc}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jxl
          --no-exif
          --no-xmp
          --no-iptc
          --dump-jxl-encoder-handoff "${_jxl_handoff}"
          "${_src_icc_jpg}"
  RESULT_VARIABLE _rv_jxl_handoff
  OUTPUT_VARIABLE _out_jxl_handoff
  ERROR_VARIABLE _err_jxl_handoff
)
if(NOT _rv_jxl_handoff EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl encoder handoff dump failed (${_rv_jxl_handoff})\nstdout:\n${_out_jxl_handoff}\nstderr:\n${_err_jxl_handoff}")
endif()
if(NOT _out_jxl_handoff MATCHES "jxl_encoder_handoff: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl encoder handoff dump missing status ok\nstdout:\n${_out_jxl_handoff}\nstderr:\n${_err_jxl_handoff}")
endif()
if(NOT EXISTS "${_jxl_handoff}")
  message(FATAL_ERROR
    "python metatransfer jxl encoder handoff dump did not write output\nstdout:\n${_out_jxl_handoff}\nstderr:\n${_err_jxl_handoff}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --load-jxl-encoder-handoff "${_jxl_handoff}"
  RESULT_VARIABLE _rv_jxl_handoff_load
  OUTPUT_VARIABLE _out_jxl_handoff_load
  ERROR_VARIABLE _err_jxl_handoff_load
)
if(NOT _rv_jxl_handoff_load EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl encoder handoff load failed (${_rv_jxl_handoff_load})\nstdout:\n${_out_jxl_handoff_load}\nstderr:\n${_err_jxl_handoff_load}")
endif()
if(NOT _out_jxl_handoff_load MATCHES "jxl_icc_profile bytes=[1-9][0-9]*")
  message(FATAL_ERROR
    "python metatransfer jxl encoder handoff load missing icc summary\nstdout:\n${_out_jxl_handoff_load}\nstderr:\n${_err_jxl_handoff_load}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --load-transfer-artifact "${_jxl_handoff}"
  RESULT_VARIABLE _rv_artifact_jxl_load
  OUTPUT_VARIABLE _out_artifact_jxl_load
  ERROR_VARIABLE _err_artifact_jxl_load
)
if(NOT _rv_artifact_jxl_load EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer generic artifact load for jxl handoff failed (${_rv_artifact_jxl_load})\nstdout:\n${_out_artifact_jxl_load}\nstderr:\n${_err_artifact_jxl_load}")
endif()
if(NOT _out_artifact_jxl_load MATCHES "transfer_artifact: status=ok code=none kind=jxl_encoder_handoff bytes=[0-9]+ target=jxl")
  message(FATAL_ERROR
    "python metatransfer generic artifact load missing jxl handoff summary\nstdout:\n${_out_artifact_jxl_load}\nstderr:\n${_err_artifact_jxl_load}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jxl
          --no-icc
          --source-meta "${_src_jpg}"
          --output "${_edited_jxl}"
          "${_target_jxl}"
  RESULT_VARIABLE _rv_jxl_edit
  OUTPUT_VARIABLE _out_jxl_edit
  ERROR_VARIABLE _err_jxl_edit
)
if(NOT _rv_jxl_edit EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl edit failed (${_rv_jxl_edit})\nstdout:\n${_out_jxl_edit}\nstderr:\n${_err_jxl_edit}")
endif()
if(NOT _out_jxl_edit MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl edit missing edit apply ok\nstdout:\n${_out_jxl_edit}\nstderr:\n${_err_jxl_edit}")
endif()
if(NOT EXISTS "${_edited_jxl}")
  message(FATAL_ERROR
    "python metatransfer jxl edit did not write output\nstdout:\n${_out_jxl_edit}\nstderr:\n${_err_jxl_edit}")
endif()
execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; import sys; b=Path(r'''${_edited_jxl}''').read_bytes(); sys.exit(0 if (len(b)>=12 and b[4:8]==b'JXL ' and b.find(b'Exif')!=-1) else 1)"
  RESULT_VARIABLE _rv_jxl_edit_check
  OUTPUT_VARIABLE _out_jxl_edit_check
  ERROR_VARIABLE _err_jxl_edit_check
)
if(NOT _rv_jxl_edit_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl edit output check failed\nstdout:\n${_out_jxl_edit}\nstderr:\n${_err_jxl_edit}\ncheck_stderr:\n${_err_jxl_edit_check}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jp2
          --no-xmp
          --no-icc
          --no-iptc
          "${_src_jpg}"
  RESULT_VARIABLE _rv_jp2
  OUTPUT_VARIABLE _out_jp2
  ERROR_VARIABLE _err_jp2
)
if(NOT _rv_jp2 EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jp2 summary failed (${_rv_jp2})\nstdout:\n${_out_jp2}\nstderr:\n${_err_jp2}")
endif()
if(NOT _out_jp2 MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "python metatransfer jp2 summary missing compile ok\nstdout:\n${_out_jp2}\nstderr:\n${_err_jp2}")
endif()
if(NOT _out_jp2 MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "python metatransfer jp2 summary missing emit ok\nstdout:\n${_out_jp2}\nstderr:\n${_err_jp2}")
endif()
if(NOT _out_jp2 MATCHES "jp2_box Exif count=1")
  message(FATAL_ERROR
    "python metatransfer jp2 summary missing Exif box summary\nstdout:\n${_out_jp2}\nstderr:\n${_err_jp2}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --source-meta "${_src_jpg}"
          --target-jp2
          --no-xmp
          --no-icc
          --no-iptc
          --output "${_edited_jp2}" --force
          "${_target_jp2}"
  RESULT_VARIABLE _rv_jp2_edit
  OUTPUT_VARIABLE _out_jp2_edit
  ERROR_VARIABLE _err_jp2_edit
)
if(NOT _rv_jp2_edit EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jp2 edit failed (${_rv_jp2_edit})\nstdout:\n${_out_jp2_edit}\nstderr:\n${_err_jp2_edit}")
endif()
if(NOT _out_jp2_edit MATCHES "edit_plan: status=ok")
  message(FATAL_ERROR
    "python metatransfer jp2 edit missing edit_plan ok\nstdout:\n${_out_jp2_edit}\nstderr:\n${_err_jp2_edit}")
endif()
if(NOT _out_jp2_edit MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer jp2 edit missing edit_apply ok\nstdout:\n${_out_jp2_edit}\nstderr:\n${_err_jp2_edit}")
endif()
if(NOT EXISTS "${_edited_jp2}")
  message(FATAL_ERROR
    "python metatransfer jp2 edit did not write output\nstdout:\n${_out_jp2_edit}\nstderr:\n${_err_jp2_edit}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jp2
          --no-xmp
          --no-icc
          --no-iptc
          "${_edited_jp2}"
  RESULT_VARIABLE _rv_jp2_roundtrip
  OUTPUT_VARIABLE _out_jp2_roundtrip
  ERROR_VARIABLE _err_jp2_roundtrip
)
if(NOT _rv_jp2_roundtrip EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jp2 roundtrip summary failed (${_rv_jp2_roundtrip})\nstdout:\n${_out_jp2_roundtrip}\nstderr:\n${_err_jp2_roundtrip}")
endif()
if(NOT _out_jp2_roundtrip MATCHES "jp2_box Exif count=1")
  message(FATAL_ERROR
    "python metatransfer jp2 roundtrip summary missing Exif box summary\nstdout:\n${_out_jp2_roundtrip}\nstderr:\n${_err_jp2_roundtrip}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-webp
          --no-xmp
          --no-icc
          --no-iptc
          "${_src_jpg}"
  RESULT_VARIABLE _rv_webp
  OUTPUT_VARIABLE _out_webp
  ERROR_VARIABLE _err_webp
)
if(NOT _rv_webp EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer webp summary failed (${_rv_webp})\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()
if(NOT _out_webp MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "python metatransfer webp summary missing compile ok\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()
if(NOT _out_webp MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "python metatransfer webp summary missing emit ok\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()
if(NOT _out_webp MATCHES "webp_chunk EXIF count=1")
  message(FATAL_ERROR
    "python metatransfer webp summary missing EXIF chunk summary\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-avif
          --no-xmp
          --no-icc
          --no-iptc
          "${_src_jpg}"
  RESULT_VARIABLE _rv_avif
  OUTPUT_VARIABLE _out_avif
  ERROR_VARIABLE _err_avif
)
if(NOT _rv_avif EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer avif summary failed (${_rv_avif})\nstdout:\n${_out_avif}\nstderr:\n${_err_avif}")
endif()
if(NOT _out_avif MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "python metatransfer avif summary missing compile ok\nstdout:\n${_out_avif}\nstderr:\n${_err_avif}")
endif()
if(NOT _out_avif MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "python metatransfer avif summary missing emit ok\nstdout:\n${_out_avif}\nstderr:\n${_err_avif}")
endif()
if(NOT _out_avif MATCHES "bmff_item Exif count=1")
  message(FATAL_ERROR
    "python metatransfer avif summary missing Exif item summary\nstdout:\n${_out_avif}\nstderr:\n${_err_avif}")
endif()

set(_avif_target "${WORK_DIR}/avif_target.bin")
execute_process(
  COMMAND python3 -c "from pathlib import Path; Path(r'${_avif_target}').write_bytes(bytes.fromhex('000000186674797068656963000000006d696631686569630000000c6d64617411223344'))"
  RESULT_VARIABLE _rv_avif_target
  OUTPUT_VARIABLE _out_avif_target
  ERROR_VARIABLE _err_avif_target
)
if(NOT _rv_avif_target EQUAL 0)
  message(FATAL_ERROR
    "failed to create AVIF target file (${_rv_avif_target})\nstdout:\n${_out_avif_target}\nstderr:\n${_err_avif_target}")
endif()

set(_avif_out "${WORK_DIR}/python_metatransfer_avif_edit.bin")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --source-meta "${_src_jpg}"
          --target-avif
          --no-xmp
          --no-icc
          --no-iptc
          --output "${_avif_out}" --force
          "${_avif_target}"
  RESULT_VARIABLE _rv_avif_edit
  OUTPUT_VARIABLE _out_avif_edit
  ERROR_VARIABLE _err_avif_edit
)
if(NOT _rv_avif_edit EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer avif edit failed (${_rv_avif_edit})\nstdout:\n${_out_avif_edit}\nstderr:\n${_err_avif_edit}")
endif()
if(NOT _out_avif_edit MATCHES "edit_plan: status=ok")
  message(FATAL_ERROR
    "python metatransfer avif edit missing edit_plan ok\nstdout:\n${_out_avif_edit}\nstderr:\n${_err_avif_edit}")
endif()
if(NOT _out_avif_edit MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer avif edit missing edit_apply ok\nstdout:\n${_out_avif_edit}\nstderr:\n${_err_avif_edit}")
endif()
if(NOT EXISTS "${_avif_out}")
  message(FATAL_ERROR
    "python metatransfer avif edit did not write output\nstdout:\n${_out_avif_edit}\nstderr:\n${_err_avif_edit}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-avif
          --no-xmp
          --no-icc
          --no-iptc
          "${_avif_out}"
  RESULT_VARIABLE _rv_avif_roundtrip
  OUTPUT_VARIABLE _out_avif_roundtrip
  ERROR_VARIABLE _err_avif_roundtrip
)
if(NOT _rv_avif_roundtrip EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer avif roundtrip summary failed (${_rv_avif_roundtrip})\nstdout:\n${_out_avif_roundtrip}\nstderr:\n${_err_avif_roundtrip}")
endif()
if(NOT _out_avif_roundtrip MATCHES "bmff_item Exif count=1")
  message(FATAL_ERROR
    "python metatransfer avif roundtrip summary missing Exif item summary\nstdout:\n${_out_avif_roundtrip}\nstderr:\n${_err_avif_roundtrip}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-avif
          --no-exif
          --no-xmp
          --no-iptc
          "${_src_icc_jpg}"
  RESULT_VARIABLE _rv_avif_icc
  OUTPUT_VARIABLE _out_avif_icc
  ERROR_VARIABLE _err_avif_icc
)
if(NOT _rv_avif_icc EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer avif icc summary failed (${_rv_avif_icc})\nstdout:\n${_out_avif_icc}\nstderr:\n${_err_avif_icc}")
endif()
if(NOT _out_avif_icc MATCHES "bmff_property colr/prof count=1")
  message(FATAL_ERROR
    "python metatransfer avif icc summary missing colr/prof property\nstdout:\n${_out_avif_icc}\nstderr:\n${_err_avif_icc}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --dump-transfer-payload-batch "${_transfer_payload_batch}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_payload_batch
  OUTPUT_VARIABLE _out_payload_batch
  ERROR_VARIABLE _err_payload_batch
)
if(NOT _rv_payload_batch EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer payload batch dump failed (${_rv_payload_batch})\nstdout:\n${_out_payload_batch}\nstderr:\n${_err_payload_batch}")
endif()
if(NOT _out_payload_batch MATCHES "transfer_payload_batch: status=ok")
  message(FATAL_ERROR
    "python metatransfer payload batch dump missing status ok\nstdout:\n${_out_payload_batch}\nstderr:\n${_err_payload_batch}")
endif()
if(NOT EXISTS "${_transfer_payload_batch}")
  message(FATAL_ERROR
    "python metatransfer payload batch dump did not write output\nstdout:\n${_out_payload_batch}\nstderr:\n${_err_payload_batch}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --load-transfer-payload-batch "${_transfer_payload_batch}"
  RESULT_VARIABLE _rv_payload_batch_load
  OUTPUT_VARIABLE _out_payload_batch_load
  ERROR_VARIABLE _err_payload_batch_load
)
if(NOT _rv_payload_batch_load EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer payload batch load failed (${_rv_payload_batch_load})\nstdout:\n${_out_payload_batch_load}\nstderr:\n${_err_payload_batch_load}")
endif()
if(NOT _out_payload_batch_load MATCHES "transfer_payload_batch: status=ok code=none bytes=[0-9]+ payloads=[0-9]+ target=jpeg")
  message(FATAL_ERROR
    "python metatransfer payload batch load missing summary\nstdout:\n${_out_payload_batch_load}\nstderr:\n${_err_payload_batch_load}")
endif()
if(NOT _out_payload_batch_load MATCHES "\\[0\\] semantic=Exif route=jpeg:app1-exif")
  message(FATAL_ERROR
    "python metatransfer payload batch load missing first payload summary\nstdout:\n${_out_payload_batch_load}\nstderr:\n${_err_payload_batch_load}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --load-transfer-artifact "${_transfer_payload_batch}"
  RESULT_VARIABLE _rv_artifact_payload_load
  OUTPUT_VARIABLE _out_artifact_payload_load
  ERROR_VARIABLE _err_artifact_payload_load
)
if(NOT _rv_artifact_payload_load EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer generic artifact load for payload batch failed (${_rv_artifact_payload_load})\nstdout:\n${_out_artifact_payload_load}\nstderr:\n${_err_artifact_payload_load}")
endif()
if(NOT _out_artifact_payload_load MATCHES "transfer_artifact: status=ok code=none kind=transfer_payload_batch bytes=[0-9]+ target=jpeg")
  message(FATAL_ERROR
    "python metatransfer generic artifact load missing payload-batch summary\nstdout:\n${_out_artifact_payload_load}\nstderr:\n${_err_artifact_payload_load}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --dump-transfer-package-batch "${_transfer_package_batch}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_package_batch
  OUTPUT_VARIABLE _out_package_batch
  ERROR_VARIABLE _err_package_batch
)
if(NOT _rv_package_batch EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer package batch dump failed (${_rv_package_batch})\nstdout:\n${_out_package_batch}\nstderr:\n${_err_package_batch}")
endif()
if(NOT _out_package_batch MATCHES "transfer_package_batch: status=ok")
  message(FATAL_ERROR
    "python metatransfer package batch dump missing status ok\nstdout:\n${_out_package_batch}\nstderr:\n${_err_package_batch}")
endif()
if(NOT EXISTS "${_transfer_package_batch}")
  message(FATAL_ERROR
    "python metatransfer package batch dump did not write output\nstdout:\n${_out_package_batch}\nstderr:\n${_err_package_batch}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --load-transfer-package-batch "${_transfer_package_batch}"
  RESULT_VARIABLE _rv_package_batch_load
  OUTPUT_VARIABLE _out_package_batch_load
  ERROR_VARIABLE _err_package_batch_load
)
if(NOT _rv_package_batch_load EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer package batch load failed (${_rv_package_batch_load})\nstdout:\n${_out_package_batch_load}\nstderr:\n${_err_package_batch_load}")
endif()
if(NOT _out_package_batch_load MATCHES "transfer_package_batch: status=ok code=none bytes=[0-9]+ chunks=[0-9]+ target=jpeg")
  message(FATAL_ERROR
    "python metatransfer package batch load missing summary\nstdout:\n${_out_package_batch_load}\nstderr:\n${_err_package_batch_load}")
endif()
if(NOT _out_package_batch_load MATCHES "\\[0\\] semantic=Exif route=jpeg:app1-exif")
  message(FATAL_ERROR
    "python metatransfer package batch load missing first chunk summary\nstdout:\n${_out_package_batch_load}\nstderr:\n${_err_package_batch_load}")
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

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jxl
          --no-exif
          --no-xmp
          --no-icc
          --no-iptc
          --c2pa-policy rewrite
          --dump-c2pa-binding "${_c2pa_jxl_binding}"
          "${_c2pa_jxl}"
  RESULT_VARIABLE _rv_jxl_binding
  OUTPUT_VARIABLE _out_jxl_binding
  ERROR_VARIABLE _err_jxl_binding
)
if(NOT _rv_jxl_binding EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl c2pa binding dump failed (${_rv_jxl_binding})\nstdout:\n${_out_jxl_binding}\nstderr:\n${_err_jxl_binding}")
endif()
if(NOT _out_jxl_binding MATCHES "c2pa_binding: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl c2pa binding dump missing status ok\nstdout:\n${_out_jxl_binding}\nstderr:\n${_err_jxl_binding}")
endif()
if(NOT EXISTS "${_c2pa_jxl_binding}")
  message(FATAL_ERROR
    "python metatransfer jxl c2pa binding dump did not write output\nstdout:\n${_out_jxl_binding}\nstderr:\n${_err_jxl_binding}")
endif()
file(READ "${_c2pa_jxl_binding}" _c2pa_jxl_binding_hex HEX)
if(NOT _c2pa_jxl_binding_hex STREQUAL "0000000c4a584c200d0a870a0000000c6a786c6311223344")
  message(FATAL_ERROR
    "python metatransfer jxl c2pa binding bytes mismatch: ${_c2pa_jxl_binding_hex}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jxl
          --no-exif
          --no-xmp
          --no-icc
          --no-iptc
          --jpeg-c2pa-signed "${_c2pa_signed_jumb}"
          --c2pa-manifest-output "${_c2pa_manifest}"
          --c2pa-certificate-chain "${_c2pa_cert}"
          --c2pa-key-ref "test-key-ref"
          --c2pa-signing-time "2026-03-09T00:00:00Z"
          --dump-c2pa-signed-package "${_c2pa_jxl_signed_package}"
          --output "${_c2pa_jxl_out}"
          "${_c2pa_jxl}"
  RESULT_VARIABLE _rv_jxl_signed_package
  OUTPUT_VARIABLE _out_jxl_signed_package
  ERROR_VARIABLE _err_jxl_signed_package
)
if(NOT _rv_jxl_signed_package EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl signed c2pa package dump failed (${_rv_jxl_signed_package})\nstdout:\n${_out_jxl_signed_package}\nstderr:\n${_err_jxl_signed_package}")
endif()
if(NOT _out_jxl_signed_package MATCHES "c2pa_signed_package: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl signed c2pa package dump missing status ok\nstdout:\n${_out_jxl_signed_package}\nstderr:\n${_err_jxl_signed_package}")
endif()
if(NOT _out_jxl_signed_package MATCHES "c2pa_stage: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl signed c2pa package dump missing stage ok\nstdout:\n${_out_jxl_signed_package}\nstderr:\n${_err_jxl_signed_package}")
endif()
if(NOT _out_jxl_signed_package MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl signed c2pa package dump missing edit ok\nstdout:\n${_out_jxl_signed_package}\nstderr:\n${_err_jxl_signed_package}")
endif()
if(NOT EXISTS "${_c2pa_jxl_signed_package}")
  message(FATAL_ERROR
    "python metatransfer jxl signed c2pa package dump did not write output\nstdout:\n${_out_jxl_signed_package}\nstderr:\n${_err_jxl_signed_package}")
endif()
if(NOT EXISTS "${_c2pa_jxl_out}")
  message(FATAL_ERROR
    "python metatransfer jxl signed c2pa edit did not write output\nstdout:\n${_out_jxl_signed_package}\nstderr:\n${_err_jxl_signed_package}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-heif
          --no-exif
          --no-xmp
          --no-icc
          --no-iptc
          --c2pa-policy rewrite
          --dump-c2pa-binding "${_c2pa_heif_binding}"
          "${_c2pa_heif}"
  RESULT_VARIABLE _rv_heif_binding
  OUTPUT_VARIABLE _out_heif_binding
  ERROR_VARIABLE _err_heif_binding
)
if(NOT _rv_heif_binding EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer heif c2pa binding dump failed (${_rv_heif_binding})\nstdout:\n${_out_heif_binding}\nstderr:\n${_err_heif_binding}")
endif()
if(NOT _out_heif_binding MATCHES "c2pa_binding: status=ok")
  message(FATAL_ERROR
    "python metatransfer heif c2pa binding dump missing status ok\nstdout:\n${_out_heif_binding}\nstderr:\n${_err_heif_binding}")
endif()
if(NOT EXISTS "${_c2pa_heif_binding}")
  message(FATAL_ERROR
    "python metatransfer heif c2pa binding dump did not write output\nstdout:\n${_out_heif_binding}\nstderr:\n${_err_heif_binding}")
endif()
file(READ "${_c2pa_heif_binding}" _c2pa_heif_binding_hex HEX)
if(NOT _c2pa_heif_binding_hex STREQUAL "000000186674797068656963000000006d696631686569630000000c6d64617411223344")
  message(FATAL_ERROR
    "python metatransfer heif c2pa binding bytes mismatch: ${_c2pa_heif_binding_hex}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-heif
          --no-exif
          --no-xmp
          --no-icc
          --no-iptc
          --c2pa-policy rewrite
          --dump-c2pa-handoff "${_c2pa_heif_handoff}"
          "${_c2pa_heif}"
  RESULT_VARIABLE _rv_heif_handoff
  OUTPUT_VARIABLE _out_heif_handoff
  ERROR_VARIABLE _err_heif_handoff
)
if(NOT _rv_heif_handoff EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer heif c2pa handoff dump failed (${_rv_heif_handoff})\nstdout:\n${_out_heif_handoff}\nstderr:\n${_err_heif_handoff}")
endif()
if(NOT _out_heif_handoff MATCHES "c2pa_handoff: status=ok")
  message(FATAL_ERROR
    "python metatransfer heif c2pa handoff dump missing status ok\nstdout:\n${_out_heif_handoff}\nstderr:\n${_err_heif_handoff}")
endif()
if(NOT EXISTS "${_c2pa_heif_handoff}")
  message(FATAL_ERROR
    "python metatransfer heif c2pa handoff dump did not write output\nstdout:\n${_out_heif_handoff}\nstderr:\n${_err_heif_handoff}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-heif
          --no-exif
          --no-xmp
          --no-icc
          --no-iptc
          --jpeg-c2pa-signed "${_c2pa_signed_jumb}"
          --c2pa-manifest-output "${_c2pa_manifest}"
          --c2pa-certificate-chain "${_c2pa_cert}"
          --c2pa-key-ref "test-key-ref"
          --c2pa-signing-time "2026-03-09T00:00:00Z"
          --dump-c2pa-signed-package "${_c2pa_heif_signed_package}"
          --output "${_c2pa_heif_out}"
          "${_c2pa_heif}"
  RESULT_VARIABLE _rv_heif_signed_package
  OUTPUT_VARIABLE _out_heif_signed_package
  ERROR_VARIABLE _err_heif_signed_package
)
if(NOT _rv_heif_signed_package EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package dump failed (${_rv_heif_signed_package})\nstdout:\n${_out_heif_signed_package}\nstderr:\n${_err_heif_signed_package}")
endif()
if(NOT _out_heif_signed_package MATCHES "c2pa_signed_package: status=ok")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package dump missing status ok\nstdout:\n${_out_heif_signed_package}\nstderr:\n${_err_heif_signed_package}")
endif()
if(NOT _out_heif_signed_package MATCHES "c2pa_stage: status=ok")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package dump missing stage ok\nstdout:\n${_out_heif_signed_package}\nstderr:\n${_err_heif_signed_package}")
endif()
if(NOT _out_heif_signed_package MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package dump missing bmff edit ok\nstdout:\n${_out_heif_signed_package}\nstderr:\n${_err_heif_signed_package}")
endif()
if(NOT EXISTS "${_c2pa_heif_signed_package}")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package dump did not write output\nstdout:\n${_out_heif_signed_package}\nstderr:\n${_err_heif_signed_package}")
endif()
if(NOT EXISTS "${_c2pa_heif_out}")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa edit did not write output\nstdout:\n${_out_heif_signed_package}\nstderr:\n${_err_heif_signed_package}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-heif
          --no-exif
          --no-xmp
          --no-icc
          --no-iptc
          --load-c2pa-signed-package "${_c2pa_heif_signed_package}"
          --output "${_c2pa_heif_from_package}"
          "${_c2pa_heif}"
  RESULT_VARIABLE _rv_heif_from_package
  OUTPUT_VARIABLE _out_heif_from_package
  ERROR_VARIABLE _err_heif_from_package
)
if(NOT _rv_heif_from_package EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package apply failed (${_rv_heif_from_package})\nstdout:\n${_out_heif_from_package}\nstderr:\n${_err_heif_from_package}")
endif()
if(NOT _out_heif_from_package MATCHES "c2pa_stage: status=ok")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package apply missing stage ok\nstdout:\n${_out_heif_from_package}\nstderr:\n${_err_heif_from_package}")
endif()
if(NOT _out_heif_from_package MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package apply missing bmff edit ok\nstdout:\n${_out_heif_from_package}\nstderr:\n${_err_heif_from_package}")
endif()
if(NOT EXISTS "${_c2pa_heif_from_package}")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package apply did not write output\nstdout:\n${_out_heif_from_package}\nstderr:\n${_err_heif_from_package}")
endif()

message(STATUS "python metatransfer edit smoke gate passed")
