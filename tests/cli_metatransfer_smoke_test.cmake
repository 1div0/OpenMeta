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
set(_target_jpg "${WORK_DIR}/target.jpg")
set(_dump_dir "${WORK_DIR}/payloads")
set(_edited_jpg "${WORK_DIR}/edited.jpg")
set(_split_jpg "${WORK_DIR}/split_injected.jpg")
set(_target_tif "${WORK_DIR}/target.tif")
set(_split_tif "${WORK_DIR}/split_injected.tif")
set(_target_tif_be "${WORK_DIR}/target_be.tif")
set(_split_tif_be "${WORK_DIR}/split_injected_be.tif")
set(_jpg_rich "${WORK_DIR}/sample_rich.jpg")
set(_c2pa_jpg "${WORK_DIR}/sample_c2pa.jpg")
set(_jumbf_box "${WORK_DIR}/sample.jumbf")
set(_signed_c2pa_box "${WORK_DIR}/signed_c2pa.jumb")
set(_signed_c2pa_manifest "${WORK_DIR}/signed_c2pa_manifest.bin")
set(_signed_c2pa_chain "${WORK_DIR}/signed_c2pa_chain.bin")
set(_c2pa_binding_out "${WORK_DIR}/sample_c2pa.binding.bin")
set(_c2pa_handoff_out "${WORK_DIR}/sample_c2pa.handoff.bin")
set(_signed_c2pa_package_out "${WORK_DIR}/sample_c2pa.signed.bin")
set(_signed_c2pa_edited "${WORK_DIR}/signed_c2pa_edited.jpg")
set(_signed_c2pa_from_package "${WORK_DIR}/signed_c2pa_from_package.jpg")
set(_split_tif_rich "${WORK_DIR}/split_rich.tif")
set(_split_tif_be_rich "${WORK_DIR}/split_rich_be.tif")
set(_rich_builder_py "${WORK_DIR}/build_rich_exif_fixture.py")
set(_rich_checker_py "${WORK_DIR}/check_rich_tiff_transfer.py")
file(MAKE_DIRECTORY "${_dump_dir}")

#Minimal JPEG with APP1 Exif payload:
#SOI + APP1(Exif + tiny TIFF IFD0 with DateTime ASCII tag) + EOI.
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

#Minimal metadata - free JPEG target(SOI + EOI)
execute_process(
  COMMAND python3 -c
    "from pathlib import Path; Path(r'''${_target_jpg}''').write_bytes(bytes([255,216,255,217]))"
  RESULT_VARIABLE _rv_write_target
  OUTPUT_VARIABLE _out_write_target
  ERROR_VARIABLE _err_write_target
)
if(NOT _rv_write_target EQUAL 0)
  message(FATAL_ERROR
    "failed to write target jpeg fixture (${_rv_write_target})\nstdout:\n${_out_write_target}\nstderr:\n${_err_write_target}")
endif()

#Minimal classic TIFF target(II + 42 + IFD0 at offset 8 with 0 entries)
execute_process(
  COMMAND python3 -c
    "from pathlib import Path; b=bytearray(); b+=b'II'; b+=(42).to_bytes(2,'little'); b+=(8).to_bytes(4,'little'); b+=(0).to_bytes(2,'little'); b+=(0).to_bytes(4,'little'); Path(r'''${_target_tif}''').write_bytes(bytes(b))"
  RESULT_VARIABLE _rv_write_tiff
  OUTPUT_VARIABLE _out_write_tiff
  ERROR_VARIABLE _err_write_tiff
)
if(NOT _rv_write_tiff EQUAL 0)
  message(FATAL_ERROR
    "failed to write target tiff fixture (${_rv_write_tiff})\nstdout:\n${_out_write_tiff}\nstderr:\n${_err_write_tiff}")
endif()

# Minimal classic big-endian TIFF target (MM + 42 + IFD0 at offset 8 with 0 entries)
execute_process(
  COMMAND python3 -c
    "from pathlib import Path; b=bytearray(); b+=b'MM'; b+=(42).to_bytes(2,'big'); b+=(8).to_bytes(4,'big'); b+=(0).to_bytes(2,'big'); b+=(0).to_bytes(4,'big'); Path(r'''${_target_tif_be}''').write_bytes(bytes(b))"
  RESULT_VARIABLE _rv_write_tiff_be
  OUTPUT_VARIABLE _out_write_tiff_be
  ERROR_VARIABLE _err_write_tiff_be
)
if(NOT _rv_write_tiff_be EQUAL 0)
  message(FATAL_ERROR
    "failed to write big-endian target tiff fixture (${_rv_write_tiff_be})\nstdout:\n${_out_write_tiff_be}\nstderr:\n${_err_write_tiff_be}")
endif()

execute_process(
  COMMAND python3 -c
    "from pathlib import Path; cbor=bytes([0xA1,0x61,0x61,0x01]); jumd=b'acme\\x00'; box=lambda t,p: (8+len(p)).to_bytes(4,'big')+t+p; payload=box(b'jumd', jumd)+box(b'cbor', cbor); Path(r'''${_jumbf_box}''').write_bytes(box(b'jumb', payload))"
  RESULT_VARIABLE _rv_write_jumbf
  OUTPUT_VARIABLE _out_write_jumbf
  ERROR_VARIABLE _err_write_jumbf
)
if(NOT _rv_write_jumbf EQUAL 0)
  message(FATAL_ERROR
    "failed to write raw jumbf fixture (${_rv_write_jumbf})\nstdout:\n${_out_write_jumbf}\nstderr:\n${_err_write_jumbf}")
endif()

execute_process(
  COMMAND python3 -c
    "from pathlib import Path; cbor=bytes([0xA1,0x61,0x61,0x01]); jumd=b'c2pa\\x00'; box=lambda t,p: (8+len(p)).to_bytes(4,'big')+t+p; jumb=box(b'jumb', box(b'jumd', jumd)+box(b'cbor', cbor)); seg=b'JP\\x00\\x00'+(1).to_bytes(4,'big')+jumb; jpg=b'\\xFF\\xD8\\xFF\\xEB'+(len(seg)+2).to_bytes(2,'big')+seg+b'\\xFF\\xD9'; Path(r'''${_c2pa_jpg}''').write_bytes(jpg)"
  RESULT_VARIABLE _rv_write_c2pa
  OUTPUT_VARIABLE _out_write_c2pa
  ERROR_VARIABLE _err_write_c2pa
)
if(NOT _rv_write_c2pa EQUAL 0)
  message(FATAL_ERROR
    "failed to write c2pa jpeg fixture (${_rv_write_c2pa})\nstdout:\n${_out_write_c2pa}\nstderr:\n${_err_write_c2pa}")
endif()

execute_process(
  COMMAND python3 -c
    "from pathlib import Path; jumd=b'c2pa\\x00'; box=lambda t,p: (8+len(p)).to_bytes(4,'big')+t+p; cbor=bytearray(); cbor+=bytes([0xA1,0x68])+b'manifest'; cbor+=bytes([0x81,0xA2,0x6F])+b'claim_generator'; cbor+=bytes([0x64])+b'test'; cbor+=bytes([0x66])+b'claims'; cbor+=bytes([0x81,0xA2,0x6A])+b'assertions'; cbor+=bytes([0x81,0xA1,0x65])+b'label'; cbor+=bytes([0x6E])+b'c2pa.hash.data'; cbor+=bytes([0x6A])+b'signatures'; cbor+=bytes([0x81,0xA2,0x63])+b'alg'; cbor+=bytes([0x65])+b'ES256'; cbor+=bytes([0x69])+b'signature'; cbor+=bytes([0x44,0x01,0x02,0x03,0x04]); Path(r'''${_signed_c2pa_box}''').write_bytes(box(b'jumb', box(b'jumd', jumd)+box(b'cbor', bytes(cbor)))); Path(r'''${_signed_c2pa_manifest}''').write_bytes(bytes(cbor)); Path(r'''${_signed_c2pa_chain}''').write_bytes(bytes([0x30,0x82,0x01,0x00]))"
  RESULT_VARIABLE _rv_write_signed_c2pa
  OUTPUT_VARIABLE _out_write_signed_c2pa
  ERROR_VARIABLE _err_write_signed_c2pa
)
if(NOT _rv_write_signed_c2pa EQUAL 0)
  message(FATAL_ERROR
    "failed to write signed c2pa fixtures (${_rv_write_signed_c2pa})\nstdout:\n${_out_write_signed_c2pa}\nstderr:\n${_err_write_signed_c2pa}")
endif()

file(WRITE "${_rich_builder_py}" [=[
import struct
import sys
from pathlib import Path

def u16le(v): return struct.pack("<H", v)
def u32le(v): return struct.pack("<I", v)
def align2(v): return (v + 1) & ~1

class Entry:
    __slots__ = ("tag", "typ", "count", "value", "order", "inline", "value_off", "dir_off")
    def __init__(self, tag, typ, count, value, order):
        self.tag = tag
        self.typ = typ
        self.count = count
        self.value = value
        self.order = order
        self.inline = False
        self.value_off = 0
        self.dir_off = 0

def add_ascii(dst, tag, s, order):
    dst.append(Entry(tag, 2, len(s) + 1, s.encode("ascii") + b"\x00", order))

def add_short(dst, tag, v, order):
    dst.append(Entry(tag, 3, 1, u16le(v), order))

def add_long(dst, tag, v, order):
    dst.append(Entry(tag, 4, 1, u32le(v), order))

def add_rational(dst, tag, pairs, order):
    raw = bytearray()
    for n, d in pairs:
        raw += u32le(n)
        raw += u32le(d)
    dst.append(Entry(tag, 5, len(pairs), bytes(raw), order))

def find_entry(dst, tag):
    for e in dst:
        if e.tag == tag:
            return e
    return None

if len(sys.argv) != 2:
    raise SystemExit("usage: build_rich_exif_fixture.py <out.jpg>")
out = Path(sys.argv[1])

ifd0 = []
exififd = []
gpsifd = []
interopifd = []

add_short(ifd0, 0x0112, 1, 0)
add_ascii(ifd0, 0x0132, "2000:01:02 03:04:05", 1)
add_long(ifd0, 0x8769, 0, 0xFFFFFFF0)
add_long(ifd0, 0x8825, 0, 0xFFFFFFF1)

add_short(exififd, 0x8827, 400, 0)
add_ascii(exififd, 0x9003, "2000:01:02 03:04:05", 1)
add_rational(exififd, 0x920A, [(66, 1)], 2)
add_long(exififd, 0xA005, 0, 0xFFFFFFF2)

add_ascii(gpsifd, 0x0001, "N", 0)
add_rational(gpsifd, 0x0002, [(41, 1), (24, 1), (5000, 100)], 1)

add_ascii(interopifd, 0x0001, "R98", 0)

ifd0.sort(key=lambda e: (e.tag, e.order))
exififd.sort(key=lambda e: (e.tag, e.order))
gpsifd.sort(key=lambda e: (e.tag, e.order))
interopifd.sort(key=lambda e: (e.tag, e.order))

cursor = 8
ifd0_off = cursor
cursor += 2 + len(ifd0) * 12 + 4
exif_off = cursor
cursor += 2 + len(exififd) * 12 + 4
gps_off = cursor
cursor += 2 + len(gpsifd) * 12 + 4
interop_off = cursor
cursor += 2 + len(interopifd) * 12 + 4

find_entry(ifd0, 0x8769).value = u32le(exif_off)
find_entry(ifd0, 0x8825).value = u32le(gps_off)
find_entry(exififd, 0xA005).value = u32le(interop_off)

for arr in (ifd0, exififd, gpsifd, interopifd):
    for e in arr:
        if len(e.value) <= 4:
            e.inline = True
        else:
            cursor = align2(cursor)
            e.inline = False
            e.value_off = cursor
            cursor += len(e.value)

tiff = bytearray(cursor)
tiff[0:2] = b"II"
tiff[2:4] = u16le(42)
tiff[4:8] = u32le(ifd0_off)

def write_ifd(buf, off, entries):
    buf[off:off+2] = u16le(len(entries))
    p = off + 2
    for e in entries:
        buf[p+0:p+2] = u16le(e.tag)
        buf[p+2:p+4] = u16le(e.typ)
        buf[p+4:p+8] = u32le(e.count)
        if e.inline:
            raw = e.value + b"\x00" * (4 - len(e.value))
            buf[p+8:p+12] = raw[:4]
        else:
            buf[p+8:p+12] = u32le(e.value_off)
        p += 12
    buf[p:p+4] = u32le(0)

write_ifd(tiff, ifd0_off, ifd0)
write_ifd(tiff, exif_off, exififd)
write_ifd(tiff, gps_off, gpsifd)
write_ifd(tiff, interop_off, interopifd)

for arr in (ifd0, exififd, gpsifd, interopifd):
    for e in arr:
        if not e.inline:
            tiff[e.value_off:e.value_off+len(e.value)] = e.value

app1 = b"Exif\x00\x00" + bytes(tiff)
jpg = b"\xFF\xD8\xFF\xE1" + struct.pack(">H", len(app1) + 2) + app1 + b"\xFF\xD9"
out.write_bytes(jpg)
]=])

execute_process(
  COMMAND python3 "${_rich_builder_py}" "${_jpg_rich}"
  RESULT_VARIABLE _rv_write_rich
  OUTPUT_VARIABLE _out_write_rich
  ERROR_VARIABLE _err_write_rich
)
if(NOT _rv_write_rich EQUAL 0)
  message(FATAL_ERROR
    "failed to write rich exif fixture (${_rv_write_rich})\nstdout:\n${_out_write_rich}\nstderr:\n${_err_write_rich}")
endif()

file(WRITE "${_rich_checker_py}" [=[
import sys
from pathlib import Path

TYPE_SIZE = {
    1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 6: 1, 7: 1,
    8: 2, 9: 4, 10: 8, 11: 4, 12: 8,
}

def fail(msg):
    print(msg, file=sys.stderr)
    raise SystemExit(1)

def read_u16(b, off, end):
    return int.from_bytes(b[off:off+2], end, signed=False)

def read_u32(b, off, end):
    return int.from_bytes(b[off:off+4], end, signed=False)

def parse_ifd(b, off, end):
    if off == 0 or off + 2 > len(b):
        fail("ifd offset out of range")
    n = read_u16(b, off, end)
    p = off + 2
    if p + n * 12 + 4 > len(b):
        fail("ifd entries truncated")
    out = {}
    for i in range(n):
        e = p + i * 12
        tag = read_u16(b, e + 0, end)
        typ = read_u16(b, e + 2, end)
        cnt = read_u32(b, e + 4, end)
        val = b[e + 8:e + 12]
        out[tag] = (typ, cnt, val)
    nxt = read_u32(b, p + n * 12, end)
    return out, nxt

def payload_for(b, entries, tag, end):
    if tag not in entries:
        fail(f"missing tag 0x{tag:04X}")
    typ, cnt, val = entries[tag]
    sz = TYPE_SIZE.get(typ, 0)
    if sz == 0:
        fail(f"unsupported tag type {typ}")
    n = sz * cnt
    if n <= 4:
        return typ, cnt, val[:n]
    off = int.from_bytes(val, end, signed=False)
    if off + n > len(b):
        fail(f"value for tag 0x{tag:04X} out of range")
    return typ, cnt, b[off:off+n]

def ascii_val(raw):
    return raw.split(b"\x00", 1)[0].decode("ascii", "ignore")

def short_val(raw, end):
    return int.from_bytes(raw[:2], end, signed=False)

def long_val(raw, end):
    return int.from_bytes(raw[:4], end, signed=False)

def rationals(raw, end):
    out = []
    for i in range(0, len(raw), 8):
        n = int.from_bytes(raw[i:i+4], end, signed=False)
        d = int.from_bytes(raw[i+4:i+8], end, signed=False)
        out.append((n, d))
    return out

if len(sys.argv) != 4:
    fail("usage: check_rich_tiff_transfer.py <tiff> <DateTime> <DateTimeOriginal>")

path = Path(sys.argv[1])
expect_dt = sys.argv[2]
expect_dto = sys.argv[3]
b = path.read_bytes()

if len(b) < 8:
    fail("tiff too small")
if b[0:2] == b"II":
    end = "little"
elif b[0:2] == b"MM":
    end = "big"
else:
    fail("invalid byte order")
if read_u16(b, 2, end) != 42:
    fail("invalid tiff magic")

ifd0_off = read_u32(b, 4, end)
ifd0, _ = parse_ifd(b, ifd0_off, end)

if 0x02BC not in ifd0:
    fail("missing XMP tag 700")

_, _, dt_raw = payload_for(b, ifd0, 0x0132, end)
if ascii_val(dt_raw) != expect_dt:
    fail("DateTime mismatch")

_, _, exif_ptr_raw = payload_for(b, ifd0, 0x8769, end)
_, _, gps_ptr_raw = payload_for(b, ifd0, 0x8825, end)
exif_off = long_val(exif_ptr_raw, end)
gps_off = long_val(gps_ptr_raw, end)
if exif_off == 0 or gps_off == 0:
    fail("missing ExifIFD/GPS pointers")

exififd, _ = parse_ifd(b, exif_off, end)
gpsifd, _ = parse_ifd(b, gps_off, end)

_, _, dto_raw = payload_for(b, exififd, 0x9003, end)
if ascii_val(dto_raw) != expect_dto:
    fail("DateTimeOriginal mismatch")

_, _, iso_raw = payload_for(b, exififd, 0x8827, end)
if short_val(iso_raw, end) != 400:
    fail("ISO value mismatch")

_, _, focal_raw = payload_for(b, exififd, 0x920A, end)
if rationals(focal_raw, end) != [(66, 1)]:
    fail("FocalLength mismatch")

_, _, interop_ptr_raw = payload_for(b, exififd, 0xA005, end)
interop_off = long_val(interop_ptr_raw, end)
if interop_off == 0:
    fail("missing InteropIFD pointer")
interopifd, _ = parse_ifd(b, interop_off, end)
_, _, interop_raw = payload_for(b, interopifd, 0x0001, end)
if ascii_val(interop_raw) != "R98":
    fail("InteropIndex mismatch")

_, _, lat_ref_raw = payload_for(b, gpsifd, 0x0001, end)
if ascii_val(lat_ref_raw) != "N":
    fail("GPSLatitudeRef mismatch")

_, _, lat_raw = payload_for(b, gpsifd, 0x0002, end)
if rationals(lat_raw, end) != [(41, 1), (24, 1), (5000, 100)]:
    fail("GPSLatitude mismatch")
]=])

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
          --no-exif --no-xmp --no-icc --no-iptc
          --jpeg-jumbf "${_jumbf_box}"
          "${_jpg}"
  RESULT_VARIABLE _rv_jumbf
  OUTPUT_VARIABLE _out_jumbf
  ERROR_VARIABLE _err_jumbf
)
if(NOT _rv_jumbf EQUAL 0)
  message(FATAL_ERROR
    "metatransfer jpeg jumbf append failed (${_rv_jumbf})\nstdout:\n${_out_jumbf}\nstderr:\n${_err_jumbf}")
endif()
if(NOT _out_jumbf MATCHES "append_jumbf: status=ok")
  message(FATAL_ERROR
    "metatransfer jpeg jumbf append missing append ok\nstdout:\n${_out_jumbf}\nstderr:\n${_err_jumbf}")
endif()
if(NOT _out_jumbf MATCHES "route=jpeg:app11-jumbf")
  message(FATAL_ERROR
    "metatransfer jpeg jumbf append missing prepared app11 route\nstdout:\n${_out_jumbf}\nstderr:\n${_err_jumbf}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --no-exif --no-xmp --no-icc --no-iptc
          --c2pa-policy invalidate
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_c2pa
  OUTPUT_VARIABLE _out_c2pa
  ERROR_VARIABLE _err_c2pa
)
if(NOT _rv_c2pa EQUAL 0)
  message(FATAL_ERROR
    "metatransfer c2pa invalidate failed (${_rv_c2pa})\nstdout:\n${_out_c2pa}\nstderr:\n${_err_c2pa}")
endif()
if(NOT _out_c2pa MATCHES "policy\\[c2pa\\]: requested=invalidate effective=keep reason=draft_invalidation_payload mode=draft_unsigned_invalidation source=content_bound output=generated_draft_unsigned_invalidation")
  message(FATAL_ERROR
    "metatransfer c2pa invalidate missing resolved policy\nstdout:\n${_out_c2pa}\nstderr:\n${_err_c2pa}")
endif()
if(NOT _out_c2pa MATCHES "route=jpeg:app11-c2pa")
  message(FATAL_ERROR
    "metatransfer c2pa invalidate missing prepared app11 c2pa route\nstdout:\n${_out_c2pa}\nstderr:\n${_err_c2pa}")
endif()
if(NOT _out_c2pa MATCHES "c2pa_rewrite: state=not_requested target=jpeg source=content_bound matched=[0-9]+ existing_segments=1 carrier_available=no invalidates_existing=no")
  message(FATAL_ERROR
    "metatransfer c2pa invalidate missing rewrite summary\nstdout:\n${_out_c2pa}\nstderr:\n${_err_c2pa}")
endif()
if(NOT _out_c2pa MATCHES "c2pa_sign_request: status=unsupported carrier=jpeg:app11-c2pa manifest_label=c2pa source_ranges=0 prepared_segments=0 bytes=0")
  message(FATAL_ERROR
    "metatransfer c2pa invalidate missing sign-request summary\nstdout:\n${_out_c2pa}\nstderr:\n${_err_c2pa}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --no-exif --no-xmp --no-icc --no-iptc
          --c2pa-policy rewrite
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_c2pa_rewrite
  OUTPUT_VARIABLE _out_c2pa_rewrite
  ERROR_VARIABLE _err_c2pa_rewrite
)
if(NOT _rv_c2pa_rewrite EQUAL 0)
  if(NOT _rv_c2pa_rewrite EQUAL 1)
    message(FATAL_ERROR
      "metatransfer c2pa rewrite returned unexpected exit (${_rv_c2pa_rewrite})\nstdout:\n${_out_c2pa_rewrite}\nstderr:\n${_err_c2pa_rewrite}")
  endif()
endif()
if(NOT _out_c2pa_rewrite MATCHES "policy\\[c2pa\\]: requested=rewrite effective=drop reason=signed_rewrite_unavailable mode=drop source=content_bound output=dropped")
  message(FATAL_ERROR
    "metatransfer c2pa rewrite missing resolved policy\nstdout:\n${_out_c2pa_rewrite}\nstderr:\n${_err_c2pa_rewrite}")
endif()
if(NOT _out_c2pa_rewrite MATCHES "c2pa_rewrite: state=signing_material_required target=jpeg source=content_bound matched=[0-9]+ existing_segments=1 carrier_available=yes invalidates_existing=yes")
  message(FATAL_ERROR
    "metatransfer c2pa rewrite missing rewrite requirements summary\nstdout:\n${_out_c2pa_rewrite}\nstderr:\n${_err_c2pa_rewrite}")
endif()
if(NOT _out_c2pa_rewrite MATCHES "c2pa_rewrite_requirements: manifest_builder=yes content_binding=yes certificate_chain=yes private_key=yes signing_time=yes")
  message(FATAL_ERROR
    "metatransfer c2pa rewrite missing rewrite requirements line\nstdout:\n${_out_c2pa_rewrite}\nstderr:\n${_err_c2pa_rewrite}")
endif()
if(NOT _out_c2pa_rewrite MATCHES "c2pa_rewrite_binding: chunks=2 bytes=4")
  message(FATAL_ERROR
    "metatransfer c2pa rewrite missing binding summary\nstdout:\n${_out_c2pa_rewrite}\nstderr:\n${_err_c2pa_rewrite}")
endif()
if(NOT _out_c2pa_rewrite MATCHES "c2pa_rewrite_chunk\\[0\\]: kind=source_range offset=0 size=2")
  message(FATAL_ERROR
    "metatransfer c2pa rewrite missing first binding chunk\nstdout:\n${_out_c2pa_rewrite}\nstderr:\n${_err_c2pa_rewrite}")
endif()
if(NOT _out_c2pa_rewrite MATCHES "c2pa_sign_request: status=ok carrier=jpeg:app11-c2pa manifest_label=c2pa source_ranges=2 prepared_segments=0 bytes=4")
  message(FATAL_ERROR
    "metatransfer c2pa rewrite missing sign-request summary\nstdout:\n${_out_c2pa_rewrite}\nstderr:\n${_err_c2pa_rewrite}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --no-exif --no-xmp --no-icc --no-iptc
          --c2pa-policy rewrite
          --dump-c2pa-binding "${_c2pa_binding_out}"
          --force
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_c2pa_binding
  OUTPUT_VARIABLE _out_c2pa_binding
  ERROR_VARIABLE _err_c2pa_binding
)
if(NOT _rv_c2pa_binding EQUAL 0)
  message(FATAL_ERROR
    "metatransfer c2pa binding dump failed (${_rv_c2pa_binding})\nstdout:\n${_out_c2pa_binding}\nstderr:\n${_err_c2pa_binding}")
endif()
if(NOT _out_c2pa_binding MATCHES "c2pa_binding: status=ok code=none bytes=4 errors=0 path=")
  message(FATAL_ERROR
    "metatransfer c2pa binding dump missing binding summary\nstdout:\n${_out_c2pa_binding}\nstderr:\n${_err_c2pa_binding}")
endif()
if(NOT EXISTS "${_c2pa_binding_out}")
  message(FATAL_ERROR "expected c2pa binding dump was not written")
endif()
file(READ "${_c2pa_binding_out}" _c2pa_binding_hex HEX)
if(NOT _c2pa_binding_hex STREQUAL "ffd8ffd9")
  message(FATAL_ERROR
    "c2pa binding dump bytes mismatch: ${_c2pa_binding_hex}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --no-exif --no-xmp --no-icc --no-iptc
          --c2pa-policy rewrite
          --dump-c2pa-handoff "${_c2pa_handoff_out}"
          --force
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_c2pa_handoff
  OUTPUT_VARIABLE _out_c2pa_handoff
  ERROR_VARIABLE _err_c2pa_handoff
)
if(NOT _rv_c2pa_handoff EQUAL 0)
  message(FATAL_ERROR
    "metatransfer c2pa handoff dump failed (${_rv_c2pa_handoff})\nstdout:\n${_out_c2pa_handoff}\nstderr:\n${_err_c2pa_handoff}")
endif()
if(NOT _out_c2pa_handoff MATCHES "c2pa_handoff_package: status=ok code=none bytes=[0-9]+ errors=0 path=")
  message(FATAL_ERROR
    "metatransfer c2pa handoff dump missing package summary\nstdout:\n${_out_c2pa_handoff}\nstderr:\n${_err_c2pa_handoff}")
endif()
if(NOT EXISTS "${_c2pa_handoff_out}")
  message(FATAL_ERROR "expected c2pa handoff package was not written")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --no-exif --no-xmp --no-icc --no-iptc
          --jpeg-c2pa-signed "${_signed_c2pa_box}"
          --c2pa-manifest-output "${_signed_c2pa_manifest}"
          --c2pa-certificate-chain "${_signed_c2pa_chain}"
          --c2pa-key-ref "test-key-ref"
          --c2pa-signing-time "2026-03-09T00:00:00Z"
          --dump-c2pa-signed-package "${_signed_c2pa_package_out}"
          --output "${_signed_c2pa_edited}" --force
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_c2pa_stage
  OUTPUT_VARIABLE _out_c2pa_stage
  ERROR_VARIABLE _err_c2pa_stage
)
if(NOT _rv_c2pa_stage EQUAL 0)
  message(FATAL_ERROR
    "metatransfer signed c2pa staging failed (${_rv_c2pa_stage})\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "prepare: status=unsupported")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing initial prepare unsupported\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "c2pa_stage_validate: status=ok code=none kind=content_bound payload_bytes=[0-9]+ carrier_bytes=[0-9]+ segments=1 errors=0")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing validation summary\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "c2pa_stage_semantics: status=ok reason=ok manifest=1 manifests=1 claim_generator=1 assertions=1 claims=1 signatures=1 linked=1 orphan=0 explicit_refs=0 unresolved=0 ambiguous=0")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing semantic summary\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "c2pa_stage_linkage: claim0_assertions=1 claim0_refs=1 sig0_links=1")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing linkage summary\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "c2pa_stage_references: sig0_keys=0 sig0_present=0 sig0_resolved=0")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing reference summary\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "c2pa_stage: status=ok code=none emitted=1 removed=0 errors=0")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing stage ok\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "policy\\[c2pa\\]: requested=rewrite effective=keep reason=external_signed_payload mode=signed_rewrite source=content_bound output=signed_rewrite")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing signed rewrite policy\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "c2pa_rewrite: state=ready target=jpeg source=content_bound matched=[0-9]+ existing_segments=1 carrier_available=yes invalidates_existing=yes")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing ready rewrite summary\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "c2pa_signed_package: status=ok code=none bytes=[0-9]+ errors=0 path=")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing signed-package summary\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing edit apply ok\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT EXISTS "${_signed_c2pa_edited}")
  message(FATAL_ERROR "expected edited signed c2pa jpeg was not written")
endif()
if(NOT EXISTS "${_signed_c2pa_package_out}")
  message(FATAL_ERROR "expected signed c2pa package was not written")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --no-exif --no-xmp --no-icc --no-iptc
          --load-c2pa-signed-package "${_signed_c2pa_package_out}"
          --output "${_signed_c2pa_from_package}" --force
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_c2pa_stage_pkg
  OUTPUT_VARIABLE _out_c2pa_stage_pkg
  ERROR_VARIABLE _err_c2pa_stage_pkg
)
if(NOT _rv_c2pa_stage_pkg EQUAL 0)
  message(FATAL_ERROR
    "metatransfer signed c2pa package staging failed (${_rv_c2pa_stage_pkg})\nstdout:\n${_out_c2pa_stage_pkg}\nstderr:\n${_err_c2pa_stage_pkg}")
endif()
if(NOT _out_c2pa_stage_pkg MATCHES "c2pa_signed_package_input: status=ok code=none bytes=[0-9]+ errors=0 path=")
  message(FATAL_ERROR
    "metatransfer signed c2pa package staging missing package input summary\nstdout:\n${_out_c2pa_stage_pkg}\nstderr:\n${_err_c2pa_stage_pkg}")
endif()
if(NOT _out_c2pa_stage_pkg MATCHES "c2pa_stage_validate: status=ok code=none kind=content_bound payload_bytes=[0-9]+ carrier_bytes=[0-9]+ segments=1 errors=0")
  message(FATAL_ERROR
    "metatransfer signed c2pa package staging missing validation summary\nstdout:\n${_out_c2pa_stage_pkg}\nstderr:\n${_err_c2pa_stage_pkg}")
endif()
if(NOT _out_c2pa_stage_pkg MATCHES "c2pa_stage_semantics: status=ok reason=ok manifest=1 manifests=1 claim_generator=1 assertions=1 claims=1 signatures=1 linked=1 orphan=0 explicit_refs=0 unresolved=0 ambiguous=0")
  message(FATAL_ERROR
    "metatransfer signed c2pa package staging missing semantic summary\nstdout:\n${_out_c2pa_stage_pkg}\nstderr:\n${_err_c2pa_stage_pkg}")
endif()
if(NOT _out_c2pa_stage_pkg MATCHES "c2pa_stage_linkage: claim0_assertions=1 claim0_refs=1 sig0_links=1")
  message(FATAL_ERROR
    "metatransfer signed c2pa package staging missing linkage summary\nstdout:\n${_out_c2pa_stage_pkg}\nstderr:\n${_err_c2pa_stage_pkg}")
endif()
if(NOT _out_c2pa_stage_pkg MATCHES "c2pa_stage_references: sig0_keys=0 sig0_present=0 sig0_resolved=0")
  message(FATAL_ERROR
    "metatransfer signed c2pa package staging missing reference summary\nstdout:\n${_out_c2pa_stage_pkg}\nstderr:\n${_err_c2pa_stage_pkg}")
endif()
if(NOT EXISTS "${_signed_c2pa_from_package}")
  message(FATAL_ERROR "expected edited jpeg from signed c2pa package was not written")
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
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-jpeg "${_target_jpg}"
          --mode metadata_rewrite
          --output "${_split_jpg}" --force
  RESULT_VARIABLE _rv_split
  OUTPUT_VARIABLE _out_split
  ERROR_VARIABLE _err_split
)
if(NOT _rv_split EQUAL 0)
  message(FATAL_ERROR
    "metatransfer source/target split edit failed (${_rv_split})\nstdout:\n${_out_split}\nstderr:\n${_err_split}")
endif()
if(NOT EXISTS "${_split_jpg}")
  message(FATAL_ERROR
    "metatransfer split mode did not write output\nstdout:\n${_out_split}\nstderr:\n${_err_split}")
endif()
execute_process(
  COMMAND python3 -c
    "from pathlib import Path; b=Path(r'''${_split_jpg}''').read_bytes(); import sys; sys.exit(0 if (len(b)>=8 and b[0]==0xFF and b[1]==0xD8 and b[2]==0xFF and b[3]==0xE1) else 1)"
  RESULT_VARIABLE _rv_split_check
  OUTPUT_VARIABLE _out_split_check
  ERROR_VARIABLE _err_split_check
)
if(NOT _rv_split_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer split output does not look like injected APP1 jpeg\nstdout:\n${_out_split}\nstderr:\n${_err_split}\ncheck_stderr:\n${_err_split_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-tiff "${_target_tif}"
          --time-patch "DateTime=2024:12:31 23:59:59"
          --output "${_split_tif}" --force
  RESULT_VARIABLE _rv_split_tif
  OUTPUT_VARIABLE _out_split_tif
  ERROR_VARIABLE _err_split_tif
)
if(NOT _rv_split_tif EQUAL 0)
  message(FATAL_ERROR
    "metatransfer source/target tiff split edit failed (${_rv_split_tif})\nstdout:\n${_out_split_tif}\nstderr:\n${_err_split_tif}")
endif()
if(NOT EXISTS "${_split_tif}")
  message(FATAL_ERROR
    "metatransfer split tiff mode did not write output\nstdout:\n${_out_split_tif}\nstderr:\n${_err_split_tif}")
endif()
execute_process(
  COMMAND python3 -c
    "from pathlib import Path; import sys; b=Path(r'''${_split_tif}''').read_bytes(); ok=(len(b)>=8 and b[0:2]==b'II' and int.from_bytes(b[2:4],'little')==42); off=int.from_bytes(b[4:8],'little') if ok else 0; ok=ok and (off+2<=len(b)); n=int.from_bytes(b[off:off+2],'little') if ok else 0; p=off+2; ok=ok and (p+n*12+4<=len(b)); tags=[int.from_bytes(b[p+i*12:p+i*12+2],'little') for i in range(n)] if ok else []; sys.exit(0 if (ok and 700 in tags and 0x0132 in tags) else 1)"
  RESULT_VARIABLE _rv_split_tif_check
  OUTPUT_VARIABLE _out_split_tif_check
  ERROR_VARIABLE _err_split_tif_check
)
if(NOT _rv_split_tif_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer split tiff output missing expected TIFF tags (700 and 0x0132)\nstdout:\n${_out_split_tif}\nstderr:\n${_err_split_tif}\ncheck_stderr:\n${_err_split_tif_check}")
endif()
execute_process(
  COMMAND python3 -c
    "from pathlib import Path; import sys; b=Path(r'''${_split_tif}''').read_bytes(); ok=(len(b)>=8 and b[0:2]==b'II' and int.from_bytes(b[2:4],'little')==42); off=int.from_bytes(b[4:8],'little') if ok else 0; ok=ok and (off+2<=len(b)); n=int.from_bytes(b[off:off+2],'little') if ok else 0; p=off+2; ok=ok and (p+n*12+4<=len(b)); dt=None; \nfor i in range(n):\n e=p+i*12; tag=int.from_bytes(b[e:e+2],'little'); typ=int.from_bytes(b[e+2:e+4],'little'); cnt=int.from_bytes(b[e+4:e+8],'little'); vo=int.from_bytes(b[e+8:e+12],'little');\n if tag==0x0132 and typ==2 and cnt>0:\n  if cnt<=4: raw=b[e+8:e+8+cnt]\n  else: raw=b[vo:vo+cnt] if vo+cnt<=len(b) else b''\n  dt=raw.split(b'\\x00',1)[0].decode('ascii','ignore')\n  break\nsys.exit(0 if (ok and dt=='2024:12:31 23:59:59') else 1)"
  RESULT_VARIABLE _rv_split_tif_dt_check
  OUTPUT_VARIABLE _out_split_tif_dt_check
  ERROR_VARIABLE _err_split_tif_dt_check
)
if(NOT _rv_split_tif_dt_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer split tiff output missing patched DateTime value\nstdout:\n${_out_split_tif}\nstderr:\n${_err_split_tif}\ncheck_stderr:\n${_err_split_tif_dt_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-tiff "${_target_tif_be}"
          --time-patch "DateTime=2024:12:31 23:59:59"
          --output "${_split_tif_be}" --force
  RESULT_VARIABLE _rv_split_tif_be
  OUTPUT_VARIABLE _out_split_tif_be
  ERROR_VARIABLE _err_split_tif_be
)
if(NOT _rv_split_tif_be EQUAL 0)
  message(FATAL_ERROR
    "metatransfer source/target big-endian tiff split edit failed (${_rv_split_tif_be})\nstdout:\n${_out_split_tif_be}\nstderr:\n${_err_split_tif_be}")
endif()
if(NOT EXISTS "${_split_tif_be}")
  message(FATAL_ERROR
    "metatransfer split big-endian tiff mode did not write output\nstdout:\n${_out_split_tif_be}\nstderr:\n${_err_split_tif_be}")
endif()
execute_process(
  COMMAND python3 -c
    "from pathlib import Path; import sys; b=Path(r'''${_split_tif_be}''').read_bytes(); ok=(len(b)>=8 and b[0:2]==b'MM' and int.from_bytes(b[2:4],'big')==42); off=int.from_bytes(b[4:8],'big') if ok else 0; ok=ok and (off+2<=len(b)); n=int.from_bytes(b[off:off+2],'big') if ok else 0; p=off+2; ok=ok and (p+n*12+4<=len(b)); entries=[(int.from_bytes(b[p+i*12:p+i*12+2],'big'), int.from_bytes(b[p+i*12+2:p+i*12+4],'big'), int.from_bytes(b[p+i*12+4:p+i*12+8],'big'), int.from_bytes(b[p+i*12+8:p+i*12+12],'big')) for i in range(n)] if ok else []; tags=[e[0] for e in entries]; dt=next(((b[p+i*12+8:p+i*12+8+cnt] if cnt<=4 else (b[vo:vo+cnt] if vo+cnt<=len(b) else b'')).split(b'\\x00',1)[0].decode('ascii','ignore') for i,(tag,typ,cnt,vo) in enumerate(entries) if tag==0x0132 and typ==2 and cnt>0), None); sys.exit(0 if (ok and 700 in tags and 0x0132 in tags and dt=='2024:12:31 23:59:59') else 1)"
  RESULT_VARIABLE _rv_split_tif_be_check
  OUTPUT_VARIABLE _out_split_tif_be_check
  ERROR_VARIABLE _err_split_tif_be_check
)
if(NOT _rv_split_tif_be_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer split big-endian tiff output missing expected tags or patched DateTime\nstdout:\n${_out_split_tif_be}\nstderr:\n${_err_split_tif_be}\ncheck_stderr:\n${_err_split_tif_be_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-jxl
          --no-xmp
          --no-icc
          --no-iptc
          "${_jpg}"
  RESULT_VARIABLE _rv_jxl
  OUTPUT_VARIABLE _out_jxl
  ERROR_VARIABLE _err_jxl
)
if(NOT _rv_jxl EQUAL 0)
  message(FATAL_ERROR
    "metatransfer jxl emit summary failed (${_rv_jxl})\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()
if(NOT _out_jxl MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "metatransfer jxl emit summary missing compile ok\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()
if(NOT _out_jxl MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "metatransfer jxl emit summary missing emit ok\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()
if(NOT _out_jxl MATCHES "jxl_box Exif count=1")
  message(FATAL_ERROR
    "metatransfer jxl emit summary missing Exif box summary\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-webp
          --no-xmp
          --no-icc
          --no-iptc
          "${_jpg}"
  RESULT_VARIABLE _rv_webp
  OUTPUT_VARIABLE _out_webp
  ERROR_VARIABLE _err_webp
)
if(NOT _rv_webp EQUAL 0)
  message(FATAL_ERROR
    "metatransfer webp emit summary failed (${_rv_webp})\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()
if(NOT _out_webp MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "metatransfer webp emit summary missing compile ok\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()
if(NOT _out_webp MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "metatransfer webp emit summary missing emit ok\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()
if(NOT _out_webp MATCHES "webp_chunk EXIF count=1")
  message(FATAL_ERROR
    "metatransfer webp emit summary missing EXIF chunk summary\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-heif
          --no-xmp
          --no-icc
          --no-iptc
          "${_jpg}"
  RESULT_VARIABLE _rv_heif
  OUTPUT_VARIABLE _out_heif
  ERROR_VARIABLE _err_heif
)
if(NOT _rv_heif EQUAL 0)
  message(FATAL_ERROR
    "metatransfer heif emit summary failed (${_rv_heif})\nstdout:\n${_out_heif}\nstderr:\n${_err_heif}")
endif()
if(NOT _out_heif MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "metatransfer heif emit summary missing compile ok\nstdout:\n${_out_heif}\nstderr:\n${_err_heif}")
endif()
if(NOT _out_heif MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "metatransfer heif emit summary missing emit ok\nstdout:\n${_out_heif}\nstderr:\n${_err_heif}")
endif()
if(NOT _out_heif MATCHES "bmff_item Exif count=1")
  message(FATAL_ERROR
    "metatransfer heif emit summary missing Exif item summary\nstdout:\n${_out_heif}\nstderr:\n${_err_heif}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg_rich}"
          --target-tiff "${_target_tif}"
          --time-patch "DateTime=2024:12:31 23:59:59"
          --time-patch "DateTimeOriginal=2024:12:31 23:59:59"
          --output "${_split_tif_rich}" --force
  RESULT_VARIABLE _rv_split_tif_rich
  OUTPUT_VARIABLE _out_split_tif_rich
  ERROR_VARIABLE _err_split_tif_rich
)
if(NOT _rv_split_tif_rich EQUAL 0)
  message(FATAL_ERROR
    "metatransfer rich split little-endian tiff edit failed (${_rv_split_tif_rich})\nstdout:\n${_out_split_tif_rich}\nstderr:\n${_err_split_tif_rich}")
endif()

execute_process(
  COMMAND python3 "${_rich_checker_py}" "${_split_tif_rich}" "2024:12:31 23:59:59" "2024:12:31 23:59:59"
  RESULT_VARIABLE _rv_split_tif_rich_check
  OUTPUT_VARIABLE _out_split_tif_rich_check
  ERROR_VARIABLE _err_split_tif_rich_check
)
if(NOT _rv_split_tif_rich_check EQUAL 0)
  message(FATAL_ERROR
    "rich little-endian tiff transfer validation failed\nstdout:\n${_out_split_tif_rich}\nstderr:\n${_err_split_tif_rich}\ncheck_stderr:\n${_err_split_tif_rich_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg_rich}"
          --target-tiff "${_target_tif_be}"
          --time-patch "DateTime=2024:12:31 23:59:59"
          --time-patch "DateTimeOriginal=2024:12:31 23:59:59"
          --output "${_split_tif_be_rich}" --force
  RESULT_VARIABLE _rv_split_tif_be_rich
  OUTPUT_VARIABLE _out_split_tif_be_rich
  ERROR_VARIABLE _err_split_tif_be_rich
)
if(NOT _rv_split_tif_be_rich EQUAL 0)
  message(FATAL_ERROR
    "metatransfer rich split big-endian tiff edit failed (${_rv_split_tif_be_rich})\nstdout:\n${_out_split_tif_be_rich}\nstderr:\n${_err_split_tif_be_rich}")
endif()

execute_process(
  COMMAND python3 "${_rich_checker_py}" "${_split_tif_be_rich}" "2024:12:31 23:59:59" "2024:12:31 23:59:59"
  RESULT_VARIABLE _rv_split_tif_be_rich_check
  OUTPUT_VARIABLE _out_split_tif_be_rich_check
  ERROR_VARIABLE _err_split_tif_be_rich_check
)
if(NOT _rv_split_tif_be_rich_check EQUAL 0)
  message(FATAL_ERROR
    "rich big-endian tiff transfer validation failed\nstdout:\n${_out_split_tif_be_rich}\nstderr:\n${_err_split_tif_be_rich}\ncheck_stderr:\n${_err_split_tif_be_rich_check}")
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
