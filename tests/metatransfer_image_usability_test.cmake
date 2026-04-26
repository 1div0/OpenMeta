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
  if(format STREQUAL "dng")
    _om_run("oiiotool create ${format}"
      "${OIIOTOOL_BIN}" --pattern checker 64x32 3 -d uint8
      -o:fileformatname=tiff "${_path}")
  else()
    _om_run("oiiotool create ${format}"
      "${OIIOTOOL_BIN}" --pattern checker 64x32 3 -d uint8 -o "${_path}")
  endif()
  set("TARGET_${format}" "${_path}" PARENT_SCOPE)
endfunction()

function(_om_check_oiio format extension)
  set(_path "${WORK_DIR}/edited.${extension}")
  execute_process(
    COMMAND "${OIIOTOOL_BIN}" --info --stats "${_path}"
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

  if(format STREQUAL "jpg")
    _om_run("metatransfer image usability ${format}"
      "${METATRANSFER_BIN}" ${_common} --target-jpeg "${_target}")
  elseif(format STREQUAL "tif")
    _om_run("metatransfer image usability ${format}"
      "${METATRANSFER_BIN}" ${_common} --target-tiff "${_target}")
  elseif(format STREQUAL "dng")
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

_om_create_target("jpg" "jpg")
_om_create_target("tif" "tif")
_om_create_target("dng" "dng")
_om_create_target("png" "png")
_om_create_target("jp2" "jp2")
_om_create_target("jxl" "jxl")

_om_transfer_and_check("jpg" "jpg")
_om_transfer_and_check("tif" "tif")
_om_transfer_and_check("dng" "dng")
_om_transfer_and_check("png" "png")
_om_transfer_and_check("jp2" "jp2")
_om_transfer_and_check("jxl" "jxl")

message(STATUS "metatransfer external image usability gate passed")
