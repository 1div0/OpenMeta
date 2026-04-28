cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED METATRANSFER_BIN OR METATRANSFER_BIN STREQUAL "")
  message(FATAL_ERROR "METATRANSFER_BIN is required")
endif()
if(NOT EXISTS "${METATRANSFER_BIN}")
  message(FATAL_ERROR "metatransfer binary not found: ${METATRANSFER_BIN}")
endif()
if(NOT DEFINED OIIOTOOL_BIN OR OIIOTOOL_BIN STREQUAL "")
  message(FATAL_ERROR "OIIOTOOL_BIN is required")
endif()
if(NOT EXISTS "${OIIOTOOL_BIN}")
  message(FATAL_ERROR "oiiotool binary not found: ${OIIOTOOL_BIN}")
endif()

if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_metatransfer_image_usability")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_source_jpg "${WORK_DIR}/source_meta.jpg")
set(_source_icc_jpg "${WORK_DIR}/source_icc.jpg")
set(_source_xmp_jpg "${WORK_DIR}/source_xmp.jpg")

execute_process(
  COMMAND python3 -c
    "from pathlib import Path; t=bytearray(); t+=b'II*\\x00'; t+=(8).to_bytes(4,'little'); t+=(1).to_bytes(2,'little'); t+=(0x0132).to_bytes(2,'little'); t+=(2).to_bytes(2,'little'); t+=(20).to_bytes(4,'little'); t+=(26).to_bytes(4,'little'); t+=(0).to_bytes(4,'little'); t+=b'2000:01:02 03:04:05\\x00'; app1=b'Exif\\x00\\x00'+bytes(t); ln=(len(app1)+2).to_bytes(2,'big'); Path(r'''${_source_jpg}''').write_bytes(b'\\xff\\xd8\\xff\\xe1'+ln+app1+b'\\xff\\xd9')"
  RESULT_VARIABLE _rv_source
  OUTPUT_VARIABLE _out_source
  ERROR_VARIABLE _err_source
)
if(NOT _rv_source EQUAL 0)
  message(FATAL_ERROR
    "failed to write image usability source fixture (${_rv_source})\nstdout:\n${_out_source}\nstderr:\n${_err_source}")
endif()

execute_process(
  COMMAND python3 -c
    "from pathlib import Path; p=bytearray(156); p[0:4]=(156).to_bytes(4,'big'); p[36:40]=b'acsp'; p[128:132]=(1).to_bytes(4,'big'); p[132:136]=b'desc'; p[136:140]=(144).to_bytes(4,'big'); p[140:144]=(12).to_bytes(4,'big'); p[144:156]=bytes([0x11])*12; app2=b'ICC_PROFILE\\x00\\x01\\x01'+bytes(p); ln=(len(app2)+2).to_bytes(2,'big'); Path(r'''${_source_icc_jpg}''').write_bytes(b'\\xff\\xd8\\xff\\xe2'+ln+app2+b'\\xff\\xd9')"
  RESULT_VARIABLE _rv_source_icc
  OUTPUT_VARIABLE _out_source_icc
  ERROR_VARIABLE _err_source_icc
)
if(NOT _rv_source_icc EQUAL 0)
  message(FATAL_ERROR
    "failed to write image usability ICC source fixture (${_rv_source_icc})\nstdout:\n${_out_source_icc}\nstderr:\n${_err_source_icc}")
endif()

execute_process(
  COMMAND python3 -c
    [=[
from pathlib import Path
import sys
p = (
    b'<x:xmpmeta xmlns:x="adobe:ns:meta/">'
    b'<rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">'
    b'<rdf:Description xmlns:dc="http://purl.org/dc/elements/1.1/">'
    b'<dc:title><rdf:Alt>'
    b'<rdf:li xml:lang="x-default">OpenMeta XMP Gate</rdf:li>'
    b'</rdf:Alt></dc:title>'
    b'</rdf:Description></rdf:RDF></x:xmpmeta>'
)
app1 = b"http://ns.adobe.com/xap/1.0/\x00" + p
Path(sys.argv[1]).write_bytes(
    b"\xff\xd8\xff\xe1" + (len(app1) + 2).to_bytes(2, "big") + app1
    + b"\xff\xd9"
)
]=]
    "${_source_xmp_jpg}"
  RESULT_VARIABLE _rv_source_xmp
  OUTPUT_VARIABLE _out_source_xmp
  ERROR_VARIABLE _err_source_xmp
)
if(NOT _rv_source_xmp EQUAL 0)
  message(FATAL_ERROR
    "failed to write image usability XMP source fixture (${_rv_source_xmp})\nstdout:\n${_out_source_xmp}\nstderr:\n${_err_source_xmp}")
endif()

function(_om_run label)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "${label} failed (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
endfunction()

function(_om_create_target format extension)
  set(_path "${WORK_DIR}/target.${extension}")
  if("${format}" STREQUAL "dng")
    _om_run("oiiotool create ${format}"
      "${OIIOTOOL_BIN}" --pattern checker 64x32 3 -d uint8
      -o:fileformatname=tiff "${_path}")
  else()
    _om_run("oiiotool create ${format}"
      "${OIIOTOOL_BIN}" --pattern checker 64x32 3 -d uint8 -o "${_path}")
  endif()
  if("${format}" STREQUAL "webp")
    _om_run("webp vp8x wrapper ${format}"
      python3 -c
        [=[
from pathlib import Path
import sys
p = Path(sys.argv[1])
b = bytearray(p.read_bytes())
if b[:4] != b"RIFF" or b[8:12] != b"WEBP":
    raise SystemExit("not webp")
vp8x = (
    b"VP8X"
    + (10).to_bytes(4, "little")
    + bytes([0, 0, 0, 0])
    + (63).to_bytes(3, "little")
    + (31).to_bytes(3, "little")
)
out = b if b[12:16] == b"VP8X" else b[:12] + vp8x + b[12:]
out[4:8] = (len(out) - 8).to_bytes(4, "little")
p.write_bytes(out)
]=]
        "${_path}")
  endif()
  set("TARGET_${format}" "${_path}" PARENT_SCOPE)
endfunction()

function(_om_check_oiio_file format path)
  execute_process(
    COMMAND "${OIIOTOOL_BIN}" --info --stats "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "oiiotool could not read edited ${format} (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "64 x[ ]+32, 3 channel")
    message(FATAL_ERROR
      "oiiotool reported unexpected geometry for edited ${format}\n${_out}")
  endif()
  if(NOT _out MATCHES "FiniteCount: 2048 2048 2048")
    message(FATAL_ERROR
      "oiiotool stats did not cover all pixels for edited ${format}\n${_out}")
  endif()
endfunction()

function(_om_check_oiio_readable_file format path)
  execute_process(
    COMMAND "${OIIOTOOL_BIN}" --info --stats "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "oiiotool could not read edited ${format} (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "FiniteCount:")
    message(FATAL_ERROR
      "oiiotool did not report pixel stats for edited ${format}\n${_out}")
  endif()
endfunction()

function(_om_check_decodable_file format path)
  execute_process(
    COMMAND "${OIIOTOOL_BIN}" --info --stats "${path}"
    RESULT_VARIABLE _rv_oiio
    OUTPUT_VARIABLE _out_oiio
    ERROR_VARIABLE _err_oiio
  )
  if(_rv_oiio EQUAL 0 AND _out_oiio MATCHES "FiniteCount:")
    return()
  endif()

  if(DEFINED FFMPEG_BIN AND NOT FFMPEG_BIN STREQUAL ""
     AND EXISTS "${FFMPEG_BIN}")
    execute_process(
      COMMAND "${FFMPEG_BIN}" -v error -i "${path}" -f null -
      RESULT_VARIABLE _rv_ffmpeg
      OUTPUT_VARIABLE _out_ffmpeg
      ERROR_VARIABLE _err_ffmpeg
    )
    if(_rv_ffmpeg EQUAL 0)
      return()
    endif()
    message(FATAL_ERROR
      "neither oiiotool nor ffmpeg could decode edited ${format}\n"
      "oiiotool stdout:\n${_out_oiio}\n"
      "oiiotool stderr:\n${_err_oiio}\n"
      "ffmpeg stdout:\n${_out_ffmpeg}\n"
      "ffmpeg stderr:\n${_err_ffmpeg}")
  endif()

  message(FATAL_ERROR
    "oiiotool could not read edited ${format}; provide FFMPEG_BIN for "
    "formats not supported by this oiiotool build\nstdout:\n${_out_oiio}\n"
    "stderr:\n${_err_oiio}")
endfunction()

function(_om_check_oiio format extension)
  _om_check_oiio_file("${format}" "${WORK_DIR}/edited.${extension}")
endfunction()

function(_om_check_exiftool format extension)
  if(NOT DEFINED EXIFTOOL_BIN OR EXIFTOOL_BIN STREQUAL ""
     OR NOT EXISTS "${EXIFTOOL_BIN}")
    return()
  endif()
  set(_path "${WORK_DIR}/edited.${extension}")
  execute_process(
    COMMAND "${EXIFTOOL_BIN}" -validate -warning -error -ImageWidth
            -ImageHeight -ExifImageWidth -ExifImageHeight -BitsPerSample
            -SamplesPerPixel -PhotometricInterpretation -PlanarConfiguration
            -Orientation -ColorSpace "${_path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "exiftool could not read edited ${format} (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(_out MATCHES "Error[ ]*:")
    message(FATAL_ERROR "exiftool reported an error for edited ${format}\n${_out}")
  endif()
  if(_out MATCHES "Improper EXIF header")
    message(FATAL_ERROR
      "exiftool reported an improper EXIF header for edited ${format}\n${_out}")
  endif()
  if(NOT _out MATCHES "Image Width[ ]*: 64")
    message(FATAL_ERROR "exiftool missing ImageWidth=64 for ${format}\n${_out}")
  endif()
  if(NOT _out MATCHES "Image Height[ ]*: 32")
    message(FATAL_ERROR "exiftool missing ImageHeight=32 for ${format}\n${_out}")
  endif()
  if(NOT _out MATCHES "Exif Image Width[ ]*: 64")
    message(FATAL_ERROR "exiftool missing ExifImageWidth=64 for ${format}\n${_out}")
  endif()
  if(NOT _out MATCHES "Exif Image Height[ ]*: 32")
    message(FATAL_ERROR "exiftool missing ExifImageHeight=32 for ${format}\n${_out}")
  endif()
  if(NOT _out MATCHES "Samples Per Pixel[ ]*: 3")
    message(FATAL_ERROR "exiftool missing SamplesPerPixel=3 for ${format}\n${_out}")
  endif()
endfunction()

function(_om_check_bmff_exif_reader_layout format path)
  execute_process(
    COMMAND "${METATRANSFER_BIN}" --no-build-info
            "--target-${format}"
            --no-xmp
            --no-icc
            --no-iptc
            "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "metatransfer could not summarize edited ${format} EXIF (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "bmff_item Exif count=1")
    message(FATAL_ERROR
      "metatransfer summary missing single BMFF Exif item for ${format}\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()

  if(DEFINED EXIFTOOL_BIN AND NOT EXIFTOOL_BIN STREQUAL ""
     AND EXISTS "${EXIFTOOL_BIN}")
    execute_process(
      COMMAND "${EXIFTOOL_BIN}" -validate -warning -error
              -ExifImageWidth -ExifImageHeight -EXIF:all "${path}"
      RESULT_VARIABLE _rv_exiftool
      OUTPUT_VARIABLE _out_exiftool
      ERROR_VARIABLE _err_exiftool
    )
    if(NOT _rv_exiftool EQUAL 0)
      message(FATAL_ERROR
        "exiftool could not read edited ${format} EXIF (${_rv_exiftool})\nstdout:\n${_out_exiftool}\nstderr:\n${_err_exiftool}")
    endif()
    if(_out_exiftool MATCHES "Error[ ]*:")
      message(FATAL_ERROR
        "exiftool reported an EXIF error for edited ${format}\n${_out_exiftool}")
    endif()
    if(_out_exiftool MATCHES "Can't currently extract Exif")
      message(FATAL_ERROR
        "exiftool could not extract BMFF Exif item layout for ${format}\n${_out_exiftool}")
    endif()
    if(NOT _out_exiftool MATCHES "Exif Image Width[ ]*: 64")
      message(FATAL_ERROR
        "exiftool missing BMFF ExifImageWidth=64 for ${format}\n${_out_exiftool}")
    endif()
    if(NOT _out_exiftool MATCHES "Exif Image Height[ ]*: 32")
      message(FATAL_ERROR
        "exiftool missing BMFF ExifImageHeight=32 for ${format}\n${_out_exiftool}")
    endif()
  endif()

endfunction()

function(_om_transfer_and_check format extension)
  set(_target "${TARGET_${format}}")
  set(_output "${WORK_DIR}/edited.${extension}")
  set(_common
    --no-build-info
    --source-meta "${_source_jpg}"
    --target-width 64
    --target-height 32
    --target-orientation 1
    --target-samples-per-pixel 3
    --target-bits-per-sample 8
    --target-sample-format 1
    --target-photometric 2
    --target-planar-configuration 1
    --target-exif-color-space 1
    --output "${_output}"
    --force)

  if("${format}" STREQUAL "jpg")
    _om_run("metatransfer image usability ${format}"
      "${METATRANSFER_BIN}" ${_common} --target-jpeg "${_target}")
  elseif("${format}" STREQUAL "tif")
    _om_run("metatransfer image usability ${format}"
      "${METATRANSFER_BIN}" ${_common} --target-tiff "${_target}")
  elseif("${format}" STREQUAL "dng")
    _om_run("metatransfer image usability ${format}"
      "${METATRANSFER_BIN}" ${_common} --target-dng "${_target}")
  else()
    _om_run("metatransfer image usability ${format}"
      "${METATRANSFER_BIN}" ${_common} "--target-${format}" "${_target}")
  endif()

  if(NOT EXISTS "${_output}")
    message(FATAL_ERROR "metatransfer did not write edited ${format}: ${_output}")
  endif()
  _om_check_oiio("${format}" "${extension}")
  _om_check_exiftool("${format}" "${extension}")
endfunction()

function(_om_transfer_bmff_if_available format extension)
  _om_prepare_bmff_target_if_available("${format}" "${extension}" "" ""
    _target _configured_target)
  if("${_target}" STREQUAL "")
    return()
  endif()

  if(_configured_target)
    _om_bmff_target_has_standard_geometry("${format}" "${_target}" _standard)
    if(NOT _standard)
      message(STATUS
        "skipping ${format} image usability check; configured target is not "
        "the 64x32 3-channel EXIF fixture shape")
      return()
    endif()
  endif()

  set("TARGET_${format}" "${_target}")
  _om_transfer_and_check("${format}" "${extension}")
  _om_check_bmff_exif_reader_layout(
    "${format}" "${WORK_DIR}/edited.${extension}")
endfunction()

function(_om_prepare_bmff_target_if_available format extension suffix label
         out_var out_configured_var)
  string(TOUPPER "${format}" _format_upper)
  set(_target_var "BMFF_${_format_upper}_TEST_TARGET")
  set(_configured_target "")
  if(DEFINED ${_target_var})
    set(_configured_target "${${_target_var}}")
  endif()

  if(NOT "${_configured_target}" STREQUAL "")
    if(NOT EXISTS "${_configured_target}")
      message(FATAL_ERROR
        "configured ${format} image usability target does not exist: "
        "${_configured_target}")
    endif()
    message(STATUS
      "using configured ${format}${label} image usability target: "
      "${_configured_target}")
    set("${out_var}" "${_configured_target}" PARENT_SCOPE)
    set("${out_configured_var}" TRUE PARENT_SCOPE)
    return()
  endif()

  set(_target "${WORK_DIR}/target${suffix}.${extension}")
  execute_process(
    COMMAND "${OIIOTOOL_BIN}" --pattern checker 64x32 3 -d uint8 -o "${_target}"
    RESULT_VARIABLE _rv_create
    OUTPUT_VARIABLE _out_create
    ERROR_VARIABLE _err_create
  )
  if(NOT _rv_create EQUAL 0)
    message(STATUS
      "skipping ${format}${label} image usability check; "
      "oiiotool could not create target")
    set("${out_var}" "" PARENT_SCOPE)
    set("${out_configured_var}" FALSE PARENT_SCOPE)
    return()
  endif()
  if(NOT EXISTS "${_target}")
    message(STATUS
      "skipping ${format}${label} image usability check; "
      "oiiotool did not write target")
    set("${out_var}" "" PARENT_SCOPE)
    set("${out_configured_var}" FALSE PARENT_SCOPE)
    return()
  endif()

  set("${out_var}" "${_target}" PARENT_SCOPE)
  set("${out_configured_var}" FALSE PARENT_SCOPE)
endfunction()

function(_om_bmff_target_has_standard_geometry format path out_var)
  execute_process(
    COMMAND "${OIIOTOOL_BIN}" --info --stats "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(STATUS
      "oiiotool could not inspect configured ${format} target geometry; "
      "skipping EXIF image-property transfer for this target")
    set("${out_var}" FALSE PARENT_SCOPE)
    return()
  endif()
  if(_out MATCHES "64 x[ ]+32, 3 channel")
    set("${out_var}" TRUE PARENT_SCOPE)
  else()
    set("${out_var}" FALSE PARENT_SCOPE)
  endif()
endfunction()

function(_om_check_bmff_icc_metadata format path)
  execute_process(
    COMMAND "${METATRANSFER_BIN}" --no-build-info
            "--target-${format}"
            --no-exif
            --no-xmp
            --no-iptc
            "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "metatransfer could not summarize edited ${format} ICC (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "bmff_property colr/prof count=1")
    message(FATAL_ERROR
      "metatransfer summary missing BMFF ICC colr/prof for ${format}\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()

  if(NOT DEFINED EXIFTOOL_BIN OR EXIFTOOL_BIN STREQUAL ""
     OR NOT EXISTS "${EXIFTOOL_BIN}")
    return()
  endif()

  execute_process(
    COMMAND "${EXIFTOOL_BIN}" -validate -warning -error -icc_profile:all
            "${path}"
    RESULT_VARIABLE _rv_exiftool
    OUTPUT_VARIABLE _out_exiftool
    ERROR_VARIABLE _err_exiftool
  )
  if(NOT _rv_exiftool EQUAL 0)
    message(FATAL_ERROR
      "exiftool could not read edited ${format} ICC (${_rv_exiftool})\nstdout:\n${_out_exiftool}\nstderr:\n${_err_exiftool}")
  endif()
  if(_out_exiftool MATCHES "Error[ ]*:")
    message(FATAL_ERROR
      "exiftool reported an ICC error for edited ${format}\n${_out_exiftool}")
  endif()
  if(_out_exiftool MATCHES "Duplicate tag 'ipma'")
    message(FATAL_ERROR
      "exiftool reported duplicate ipma for edited ${format}\n${_out_exiftool}")
  endif()
  if(NOT _out_exiftool MATCHES "Profile File Signature[ ]*: acsp")
    message(FATAL_ERROR
      "exiftool did not find the transferred ICC profile for ${format}\n${_out_exiftool}")
  endif()
endfunction()

function(_om_check_bmff_xmp_metadata format path)
  execute_process(
    COMMAND "${METATRANSFER_BIN}" --no-build-info
            "--target-${format}"
            --no-exif
            --no-icc
            --no-iptc
            "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "metatransfer could not summarize edited ${format} XMP (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "bmff_item mime/xmp count=1")
    message(FATAL_ERROR
      "metatransfer summary missing single BMFF XMP item for ${format}\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()

  if(NOT DEFINED EXIFTOOL_BIN OR EXIFTOOL_BIN STREQUAL ""
     OR NOT EXISTS "${EXIFTOOL_BIN}")
    return()
  endif()

  execute_process(
    COMMAND "${EXIFTOOL_BIN}" -validate -warning -error -XMP:all "${path}"
    RESULT_VARIABLE _rv_exiftool
    OUTPUT_VARIABLE _out_exiftool
    ERROR_VARIABLE _err_exiftool
  )
  if(NOT _rv_exiftool EQUAL 0)
    message(FATAL_ERROR
      "exiftool could not read edited ${format} XMP (${_rv_exiftool})\nstdout:\n${_out_exiftool}\nstderr:\n${_err_exiftool}")
  endif()
  if(_out_exiftool MATCHES "Error[ ]*:")
    message(FATAL_ERROR
      "exiftool reported an XMP error for edited ${format}\n${_out_exiftool}")
  endif()
  if(_out_exiftool MATCHES "Duplicate tag 'ipma'")
    message(FATAL_ERROR
      "exiftool reported duplicate ipma for edited ${format}\n${_out_exiftool}")
  endif()
  if("${format}" STREQUAL "cr3")
    return()
  endif()
  if(NOT _out_exiftool MATCHES "Title[ ]*: OpenMeta XMP Gate")
    message(FATAL_ERROR
      "exiftool did not find the transferred XMP title for ${format}\n${_out_exiftool}")
  endif()
endfunction()

function(_om_transfer_bmff_icc_if_available format extension)
  _om_prepare_bmff_target_if_available("${format}" "${extension}" "_icc"
    " ICC" _target _configured_target)
  if("${_target}" STREQUAL "")
    return()
  endif()

  set(_output "${WORK_DIR}/edited_icc.${extension}")

  _om_run("metatransfer image usability ${format} ICC"
    "${METATRANSFER_BIN}" --no-build-info
    --source-meta "${_source_icc_jpg}"
    --no-exif
    --no-xmp
    --no-iptc
    --output "${_output}"
    --force
    "--target-${format}" "${_target}")

  if(NOT EXISTS "${_output}")
    message(FATAL_ERROR
      "metatransfer did not write edited ${format} ICC output: ${_output}")
  endif()
  _om_check_decodable_file("${format}" "${_output}")
  _om_check_bmff_icc_metadata("${format}" "${_output}")
endfunction()

function(_om_transfer_bmff_xmp_if_available format extension)
  _om_prepare_bmff_target_if_available("${format}" "${extension}" "_xmp"
    " XMP" _target _configured_target)
  if("${_target}" STREQUAL "")
    return()
  endif()

  set(_output "${WORK_DIR}/edited_xmp.${extension}")

  _om_run("metatransfer image usability ${format} XMP"
    "${METATRANSFER_BIN}" --no-build-info
    --source-meta "${_source_xmp_jpg}"
    --no-exif
    --no-icc
    --no-iptc
    --output "${_output}"
    --force
    "--target-${format}" "${_target}")

  if(NOT EXISTS "${_output}")
    message(FATAL_ERROR
      "metatransfer did not write edited ${format} XMP output: ${_output}")
  endif()
  _om_check_decodable_file("${format}" "${_output}")
  _om_check_bmff_xmp_metadata("${format}" "${_output}")
endfunction()

_om_create_target("jpg" "jpg")
_om_create_target("tif" "tif")
_om_create_target("dng" "dng")
_om_create_target("png" "png")
_om_create_target("webp" "webp")
_om_create_target("jp2" "jp2")
_om_create_target("jxl" "jxl")

_om_transfer_and_check("jpg" "jpg")
_om_transfer_and_check("tif" "tif")
_om_transfer_and_check("dng" "dng")
_om_transfer_and_check("png" "png")
_om_transfer_and_check("webp" "webp")
_om_transfer_and_check("jp2" "jp2")
_om_transfer_and_check("jxl" "jxl")

_om_transfer_bmff_if_available("heif" "heic")
_om_transfer_bmff_if_available("avif" "avif")
_om_transfer_bmff_if_available("cr3" "cr3")
_om_transfer_bmff_icc_if_available("heif" "heic")
_om_transfer_bmff_icc_if_available("avif" "avif")
_om_transfer_bmff_icc_if_available("cr3" "cr3")
_om_transfer_bmff_xmp_if_available("heif" "heic")
_om_transfer_bmff_xmp_if_available("avif" "avif")
_om_transfer_bmff_xmp_if_available("cr3" "cr3")

message(STATUS "metatransfer external image usability gate passed")
