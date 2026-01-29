#include "openmeta/exif_tiff_decode.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <array>
#include <cstring>

namespace openmeta {
namespace {

    static constexpr uint8_t u8(std::byte b) noexcept
    {
        return static_cast<uint8_t>(b);
    }

    static bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 8U)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 0U);
        *out = v;
        return true;
    }

    static bool read_u16le(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 0U)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 8U);
        *out = v;
        return true;
    }

    static bool read_u32be(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (offset + 4 > bytes.size()) {
            return false;
        }
        const uint32_t v
            = (static_cast<uint32_t>(u8(bytes[offset + 0])) << 24U)
              | (static_cast<uint32_t>(u8(bytes[offset + 1])) << 16U)
              | (static_cast<uint32_t>(u8(bytes[offset + 2])) << 8U)
              | (static_cast<uint32_t>(u8(bytes[offset + 3])) << 0U);
        *out = v;
        return true;
    }

    static bool read_u32le(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (offset + 4 > bytes.size()) {
            return false;
        }
        const uint32_t v
            = (static_cast<uint32_t>(u8(bytes[offset + 0])) << 0U)
              | (static_cast<uint32_t>(u8(bytes[offset + 1])) << 8U)
              | (static_cast<uint32_t>(u8(bytes[offset + 2])) << 16U)
              | (static_cast<uint32_t>(u8(bytes[offset + 3])) << 24U);
        *out = v;
        return true;
    }

    static bool read_u64be(std::span<const std::byte> bytes, uint64_t offset,
                           uint64_t* out) noexcept
    {
        if (offset + 8 > bytes.size()) {
            return false;
        }
        uint64_t v = 0;
        for (uint32_t i = 0; i < 8; ++i) {
            v = (v << 8U) | static_cast<uint64_t>(u8(bytes[offset + i]));
        }
        *out = v;
        return true;
    }

    static bool read_u64le(std::span<const std::byte> bytes, uint64_t offset,
                           uint64_t* out) noexcept
    {
        if (offset + 8 > bytes.size()) {
            return false;
        }
        uint64_t v = 0;
        for (uint32_t i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(u8(bytes[offset + i])) << (i * 8U);
        }
        *out = v;
        return true;
    }

    struct TiffConfig final {
        bool le      = true;
        bool bigtiff = false;
    };

    static bool read_tiff_u16(const TiffConfig& cfg,
                              std::span<const std::byte> bytes, uint64_t offset,
                              uint16_t* out) noexcept
    {
        if (cfg.le) {
            return read_u16le(bytes, offset, out);
        }
        return read_u16be(bytes, offset, out);
    }

    static bool read_tiff_u32(const TiffConfig& cfg,
                              std::span<const std::byte> bytes, uint64_t offset,
                              uint32_t* out) noexcept
    {
        if (cfg.le) {
            return read_u32le(bytes, offset, out);
        }
        return read_u32be(bytes, offset, out);
    }

    static bool read_tiff_u64(const TiffConfig& cfg,
                              std::span<const std::byte> bytes, uint64_t offset,
                              uint64_t* out) noexcept
    {
        if (cfg.le) {
            return read_u64le(bytes, offset, out);
        }
        return read_u64be(bytes, offset, out);
    }

    static uint64_t tiff_type_size(uint16_t type) noexcept
    {
        switch (type) {
        case 1:    // BYTE
        case 2:    // ASCII
        case 6:    // SBYTE
        case 7:    // UNDEFINED
        case 129:  // UTF-8 (EXIF)
            return 1;
        case 3:  // SHORT
        case 8:  // SSHORT
            return 2;
        case 4:   // LONG
        case 9:   // SLONG
        case 11:  // FLOAT
        case 13:  // IFD
            return 4;
        case 5:   // RATIONAL
        case 10:  // SRATIONAL
        case 12:  // DOUBLE
            return 8;
        case 16:  // LONG8
        case 17:  // SLONG8
        case 18:  // IFD8
            return 8;
        default: return 0;
        }
    }

    static bool contains_nul(std::span<const std::byte> bytes) noexcept
    {
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (bytes[i] == std::byte { 0 }) {
                return true;
            }
        }
        return false;
    }

    static uint32_t write_u32_decimal(char* out, uint32_t value) noexcept
    {
        char tmp[16];
        uint32_t tmp_len = 0;
        do {
            const uint32_t digit = value % 10U;
            tmp[tmp_len]         = static_cast<char>('0' + digit);
            tmp_len += 1;
            value /= 10U;
        } while (value != 0U && tmp_len < sizeof(tmp));

        for (uint32_t i = 0; i < tmp_len; ++i) {
            out[i] = tmp[tmp_len - 1 - i];
        }
        return tmp_len;
    }

    static std::string_view ifd_token(ExifIfdKind kind, uint32_t index,
                                      std::span<char> scratch) noexcept
    {
        switch (kind) {
        case ExifIfdKind::Ifd: {
            if (scratch.size() < 16) {
                return std::string_view();
            }
            scratch[0]            = 'i';
            scratch[1]            = 'f';
            scratch[2]            = 'd';
            const uint32_t digits = write_u32_decimal(scratch.data() + 3,
                                                      index);
            return std::string_view(scratch.data(), 3U + digits);
        }
        case ExifIfdKind::ExifIfd: return "exififd";
        case ExifIfdKind::GpsIfd: return "gpsifd";
        case ExifIfdKind::InteropIfd: return "interopifd";
        case ExifIfdKind::SubIfd: {
            if (scratch.size() < 20) {
                return std::string_view();
            }
            const char prefix[] = "subifd";
            std::memcpy(scratch.data(), prefix, sizeof(prefix) - 1);
            const uint32_t digits = write_u32_decimal(scratch.data() + 6,
                                                      index);
            return std::string_view(scratch.data(), 6U + digits);
        }
        }
        return std::string_view();
    }

    struct IfdTask final {
        ExifIfdKind kind = ExifIfdKind::Ifd;
        uint32_t index   = 0;
        uint64_t offset  = 0;
    };

    struct IfdSink final {
        ExifIfdRef* out = nullptr;
        uint32_t cap    = 0;
        ExifDecodeResult result;
    };

    static void sink_emit(IfdSink* sink, const ExifIfdRef& ref) noexcept
    {
        sink->result.ifds_needed += 1;
        if (sink->result.ifds_written < sink->cap) {
            sink->out[sink->result.ifds_written] = ref;
            sink->result.ifds_written += 1;
        } else if (sink->result.status == ExifDecodeStatus::Ok) {
            sink->result.status = ExifDecodeStatus::OutputTruncated;
        }
    }

    static uint8_t ifd_kind_bit(ExifIfdKind kind) noexcept
    {
        switch (kind) {
        case ExifIfdKind::Ifd: return 1U << 0U;
        case ExifIfdKind::ExifIfd: return 1U << 1U;
        case ExifIfdKind::GpsIfd: return 1U << 2U;
        case ExifIfdKind::InteropIfd: return 1U << 3U;
        case ExifIfdKind::SubIfd: return 1U << 4U;
        default: break;
        }
        return 0;
    }

    static uint32_t find_visited(uint64_t off,
                                 std::span<const uint64_t> visited_offs,
                                 uint32_t visited_count) noexcept
    {
        for (uint32_t i = 0; i < visited_count; ++i) {
            if (visited_offs[i] == off) {
                return i;
            }
        }
        return 0xffffffffU;
    }

    static bool allow_revisit_kind(ExifIfdKind kind,
                                  uint8_t existing_mask) noexcept
    {
        // In some malformed files, GPSInfoIFDPointer references the same IFD as
        // InteropIFDPointer. ExifTool reports both groups. Preserve that
        // behavior by allowing a second decode pass for the GPS/Interop pair.
        const uint8_t gps    = ifd_kind_bit(ExifIfdKind::GpsIfd);
        const uint8_t intero = ifd_kind_bit(ExifIfdKind::InteropIfd);

        if (kind == ExifIfdKind::GpsIfd) {
            return existing_mask == intero;
        }
        if (kind == ExifIfdKind::InteropIfd) {
            return existing_mask == gps;
        }
        return false;
    }

    static uint8_t ifd_priority(ExifIfdKind kind) noexcept
    {
        // Prefer structured sub-directories over generic IFD chain when offsets
        // collide in malformed files (observed in the ExifTool sample corpus).
        switch (kind) {
        case ExifIfdKind::ExifIfd: return 5;
        case ExifIfdKind::InteropIfd: return 4;
        case ExifIfdKind::GpsIfd: return 3;
        case ExifIfdKind::SubIfd: return 2;
        case ExifIfdKind::Ifd: return 1;
        default: break;
        }
        return 0;
    }

    static uint32_t select_next_task_index(std::span<const IfdTask> tasks,
                                          uint32_t task_count) noexcept
    {
        uint32_t best_index   = 0;
        uint8_t best_priority = 0;
        uint64_t best_off     = 0;

        for (uint32_t i = 0; i < task_count; ++i) {
            const uint8_t prio = ifd_priority(tasks[i].kind);
            const uint64_t off = tasks[i].offset;
            if (i == 0 || prio > best_priority
                || (prio == best_priority && off < best_off)) {
                best_index   = i;
                best_priority = prio;
                best_off      = off;
            }
        }
        return best_index;
    }

    static void update_status(ExifDecodeResult* out,
                              ExifDecodeStatus status) noexcept
    {
        if (out->status == ExifDecodeStatus::LimitExceeded) {
            return;
        }
        if (status == ExifDecodeStatus::LimitExceeded) {
            out->status = status;
            return;
        }
        if (out->status == ExifDecodeStatus::Malformed) {
            return;
        }
        if (status == ExifDecodeStatus::Malformed) {
            out->status = status;
            return;
        }
        if (out->status == ExifDecodeStatus::Unsupported) {
            return;
        }
        if (status == ExifDecodeStatus::Unsupported) {
            out->status = status;
            return;
        }
        if (out->status == ExifDecodeStatus::OutputTruncated) {
            return;
        }
        if (status == ExifDecodeStatus::OutputTruncated) {
            out->status = status;
            return;
        }
    }

    static bool decode_u32_or_u64_offset(const TiffConfig& cfg,
                                         std::span<const std::byte> bytes,
                                         uint16_t type, uint64_t value_off,
                                         uint64_t count,
                                         std::span<uint64_t> out_ptrs,
                                         uint32_t* out_count) noexcept
    {
        *out_count = 0;

        const uint64_t unit = tiff_type_size(type);
        if (unit == 0) {
            return false;
        }
        if (count > (UINT64_MAX / unit)) {
            return false;
        }
        const uint64_t total_bytes = count * unit;
        if (value_off + total_bytes > bytes.size()) {
            return false;
        }

        const uint32_t cap = static_cast<uint32_t>(out_ptrs.size());
        const uint32_t n   = (count < cap) ? static_cast<uint32_t>(count) : cap;
        for (uint32_t i = 0; i < n; ++i) {
            uint64_t ptr = 0;
            if (unit == 4) {
                uint32_t v = 0;
                if (!read_tiff_u32(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 4U,
                                   &v)) {
                    break;
                }
                ptr = v;
            } else if (unit == 8) {
                if (!read_tiff_u64(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 8U,
                                   &ptr)) {
                    break;
                }
            } else {
                break;
            }
            out_ptrs[i] = ptr;
            *out_count += 1;
        }
        return true;
    }

    static MetaValue decode_text_value(ByteArena& arena,
                                       std::span<const std::byte> raw,
                                       TextEncoding enc) noexcept
    {
        if (raw.empty()) {
            return make_text(arena, std::string_view(), enc);
        }

        size_t trimmed = raw.size();
        if (raw[trimmed - 1] == std::byte { 0 }) {
            trimmed -= 1;
        }
        const std::span<const std::byte> payload = raw.subspan(0, trimmed);
        if (contains_nul(payload)) {
            return make_bytes(arena, raw);
        }

        const std::string_view text(reinterpret_cast<const char*>(
                                        payload.data()),
                                    payload.size());
        return make_text(arena, text, enc);
    }

    static MetaValue decode_tiff_value(const TiffConfig& cfg,
                                       std::span<const std::byte> bytes,
                                       uint16_t type, uint64_t count,
                                       uint64_t value_off, uint64_t value_bytes,
                                       ByteArena& arena,
                                       const ExifDecodeLimits& limits,
                                       ExifDecodeResult* result) noexcept
    {
        if (value_bytes > limits.max_value_bytes) {
            update_status(result, ExifDecodeStatus::LimitExceeded);
            return MetaValue {};
        }

        switch (type) {
        case 1: {  // BYTE
            if (count == 1) {
                return make_u8(u8(bytes[value_off]));
            }
            const uint32_t n = (count > UINT32_MAX)
                                   ? UINT32_MAX
                                   : static_cast<uint32_t>(count);
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::U8;
            v.count     = n;
            v.data.span = arena.append(
                bytes.subspan(value_off, static_cast<size_t>(value_bytes)));
            return v;
        }
        case 2: {  // ASCII
            return decode_text_value(
                arena,
                bytes.subspan(value_off, static_cast<size_t>(value_bytes)),
                TextEncoding::Ascii);
        }
        case 3: {  // SHORT
            if (count == 1) {
                uint16_t v = 0;
                if (!read_tiff_u16(cfg, bytes, value_off, &v)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_u16(v);
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::U16;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 2U),
                                         alignof(uint16_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint16_t value = 0;
                if (!read_tiff_u16(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 2U,
                                   &value)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                std::memcpy(dst.data() + i * 2U, &value, 2U);
            }
            return v;
        }
        case 4:     // LONG
        case 13: {  // IFD
            if (count == 1) {
                uint32_t v = 0;
                if (!read_tiff_u32(cfg, bytes, value_off, &v)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_u32(v);
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::U32;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 4U),
                                         alignof(uint32_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint32_t value = 0;
                if (!read_tiff_u32(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 4U,
                                   &value)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                std::memcpy(dst.data() + i * 4U, &value, 4U);
            }
            return v;
        }
        case 5: {  // RATIONAL
            if (count == 1) {
                uint32_t numer = 0;
                uint32_t denom = 0;
                if (!read_tiff_u32(cfg, bytes, value_off + 0, &numer)
                    || !read_tiff_u32(cfg, bytes, value_off + 4, &denom)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_urational(numer, denom);
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::URational;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(
                                             count * sizeof(URational)),
                                         alignof(URational));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint32_t numer      = 0;
                uint32_t denom      = 0;
                const uint64_t base = value_off + static_cast<uint64_t>(i) * 8U;
                if (!read_tiff_u32(cfg, bytes, base + 0, &numer)
                    || !read_tiff_u32(cfg, bytes, base + 4, &denom)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                const URational r { numer, denom };
                std::memcpy(dst.data() + i * sizeof(URational), &r,
                            sizeof(URational));
            }
            return v;
        }
        case 6: {  // SBYTE
            if (count == 1) {
                return make_i8(static_cast<int8_t>(u8(bytes[value_off])));
            }
            const uint32_t n = (count > UINT32_MAX)
                                   ? UINT32_MAX
                                   : static_cast<uint32_t>(count);
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::I8;
            v.count     = n;
            v.data.span = arena.append(
                bytes.subspan(value_off, static_cast<size_t>(value_bytes)));
            return v;
        }
        case 7: {  // UNDEFINED
            return make_bytes(arena,
                              bytes.subspan(value_off,
                                            static_cast<size_t>(value_bytes)));
        }
        case 8: {  // SSHORT
            if (count == 1) {
                uint16_t raw = 0;
                if (!read_tiff_u16(cfg, bytes, value_off, &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_i16(static_cast<int16_t>(raw));
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::I16;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 2U),
                                         alignof(int16_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint16_t raw = 0;
                if (!read_tiff_u16(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 2U,
                                   &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                const int16_t value = static_cast<int16_t>(raw);
                std::memcpy(dst.data() + i * 2U, &value, 2U);
            }
            return v;
        }
        case 9: {  // SLONG
            if (count == 1) {
                uint32_t raw = 0;
                if (!read_tiff_u32(cfg, bytes, value_off, &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_i32(static_cast<int32_t>(raw));
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::I32;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 4U),
                                         alignof(int32_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint32_t raw = 0;
                if (!read_tiff_u32(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 4U,
                                   &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                const int32_t value = static_cast<int32_t>(raw);
                std::memcpy(dst.data() + i * 4U, &value, 4U);
            }
            return v;
        }
        case 10: {  // SRATIONAL
            if (count == 1) {
                uint32_t numer_u = 0;
                uint32_t denom_u = 0;
                if (!read_tiff_u32(cfg, bytes, value_off + 0, &numer_u)
                    || !read_tiff_u32(cfg, bytes, value_off + 4, &denom_u)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_srational(static_cast<int32_t>(numer_u),
                                      static_cast<int32_t>(denom_u));
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::SRational;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(
                                             count * sizeof(SRational)),
                                         alignof(SRational));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint32_t numer_u    = 0;
                uint32_t denom_u    = 0;
                const uint64_t base = value_off + static_cast<uint64_t>(i) * 8U;
                if (!read_tiff_u32(cfg, bytes, base + 0, &numer_u)
                    || !read_tiff_u32(cfg, bytes, base + 4, &denom_u)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                const SRational r { static_cast<int32_t>(numer_u),
                                    static_cast<int32_t>(denom_u) };
                std::memcpy(dst.data() + i * sizeof(SRational), &r,
                            sizeof(SRational));
            }
            return v;
        }
        case 11: {  // FLOAT
            if (count == 1) {
                uint32_t bits = 0;
                if (!read_tiff_u32(cfg, bytes, value_off, &bits)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_f32_bits(bits);
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::F32;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 4U),
                                         alignof(uint32_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint32_t bits = 0;
                if (!read_tiff_u32(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 4U,
                                   &bits)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                std::memcpy(dst.data() + i * 4U, &bits, 4U);
            }
            return v;
        }
        case 12: {  // DOUBLE
            if (count == 1) {
                uint64_t bits = 0;
                if (!read_tiff_u64(cfg, bytes, value_off, &bits)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_f64_bits(bits);
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::F64;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 8U),
                                         alignof(uint64_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint64_t bits = 0;
                if (!read_tiff_u64(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 8U,
                                   &bits)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                std::memcpy(dst.data() + i * 8U, &bits, 8U);
            }
            return v;
        }
        case 16:    // LONG8
        case 18: {  // IFD8
            if (count == 1) {
                uint64_t v = 0;
                if (!read_tiff_u64(cfg, bytes, value_off, &v)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_u64(v);
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::U64;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 8U),
                                         alignof(uint64_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint64_t value = 0;
                if (!read_tiff_u64(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 8U,
                                   &value)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                std::memcpy(dst.data() + i * 8U, &value, 8U);
            }
            return v;
        }
        case 17: {  // SLONG8
            if (count == 1) {
                uint64_t raw = 0;
                if (!read_tiff_u64(cfg, bytes, value_off, &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_i64(static_cast<int64_t>(raw));
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::I64;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 8U),
                                         alignof(int64_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint64_t raw = 0;
                if (!read_tiff_u64(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 8U,
                                   &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                const int64_t value = static_cast<int64_t>(raw);
                std::memcpy(dst.data() + i * 8U, &value, 8U);
            }
            return v;
        }
        case 129: {  // UTF-8 (EXIF)
            return decode_text_value(
                arena,
                bytes.subspan(value_off, static_cast<size_t>(value_bytes)),
                TextEncoding::Utf8);
        }
        default: break;
        }

        return MetaValue {};
    }

    static bool
    follow_ifd_pointers(const TiffConfig& cfg, std::span<const std::byte> bytes,
                        uint16_t tag, uint16_t type, uint64_t count,
                        uint64_t value_off, std::span<IfdTask> stack,
                        uint32_t* stack_size, uint32_t* next_subifd_index,
                        const ExifDecodeLimits& limits,
                        ExifDecodeResult* result) noexcept
    {
        if (tag != 0x8769 && tag != 0x8825 && tag != 0xA005 && tag != 0x014A) {
            return true;
        }

        if (*stack_size >= limits.max_ifds) {
            update_status(result, ExifDecodeStatus::LimitExceeded);
            return false;
        }

        std::array<uint64_t, 32> ptrs {};
        uint32_t ptr_count = 0;
        if (!decode_u32_or_u64_offset(cfg, bytes, type, value_off, count,
                                      std::span<uint64_t>(ptrs), &ptr_count)) {
            return true;
        }

        if (tag == 0x014A) {  // SubIFDs: may be an array of offsets.
            for (uint32_t i = 0; i < ptr_count; ++i) {
                if (*stack_size >= stack.size()
                    || *stack_size >= limits.max_ifds) {
                    update_status(result, ExifDecodeStatus::LimitExceeded);
                    return false;
                }
                IfdTask t;
                t.kind  = ExifIfdKind::SubIfd;
                t.index = *next_subifd_index;
                *next_subifd_index += 1;
                t.offset           = ptrs[i];
                stack[*stack_size] = t;
                *stack_size += 1;
            }
            return true;
        }

        if (ptr_count == 0) {
            return true;
        }

        IfdTask t;
        t.offset = ptrs[0];
        t.index  = 0;
        if (tag == 0x8769) {
            t.kind = ExifIfdKind::ExifIfd;
        } else if (tag == 0x8825) {
            t.kind = ExifIfdKind::GpsIfd;
        } else if (tag == 0xA005) {
            t.kind = ExifIfdKind::InteropIfd;
        } else {
            return true;
        }

        if (*stack_size < stack.size() && *stack_size < limits.max_ifds) {
            stack[*stack_size] = t;
            *stack_size += 1;
        } else {
            update_status(result, ExifDecodeStatus::LimitExceeded);
            return false;
        }

        return true;
    }

}  // namespace

ExifDecodeResult
decode_exif_tiff(std::span<const std::byte> tiff_bytes, MetaStore& store,
                 std::span<ExifIfdRef> out_ifds,
                 const ExifDecodeOptions& options) noexcept
{
    IfdSink sink;
    sink.out = out_ifds.data();
    sink.cap = static_cast<uint32_t>(out_ifds.size());

    if (tiff_bytes.size() < 8) {
        sink.result.status = ExifDecodeStatus::Malformed;
        return sink.result;
    }

    TiffConfig cfg;
    const uint8_t b0 = u8(tiff_bytes[0]);
    const uint8_t b1 = u8(tiff_bytes[1]);
    if (b0 == 0x49 && b1 == 0x49) {
        cfg.le = true;
    } else if (b0 == 0x4D && b1 == 0x4D) {
        cfg.le = false;
    } else {
        sink.result.status = ExifDecodeStatus::Unsupported;
        return sink.result;
    }

    uint16_t version = 0;
    if (!read_tiff_u16(cfg, tiff_bytes, 2, &version)) {
        sink.result.status = ExifDecodeStatus::Malformed;
        return sink.result;
    }
    if (version == 42) {
        cfg.bigtiff = false;
    } else if (version == 43) {
        cfg.bigtiff = true;
    } else {
        sink.result.status = ExifDecodeStatus::Unsupported;
        return sink.result;
    }

    uint64_t first_ifd = 0;
    if (!cfg.bigtiff) {
        uint32_t off32 = 0;
        if (!read_tiff_u32(cfg, tiff_bytes, 4, &off32)) {
            sink.result.status = ExifDecodeStatus::Malformed;
            return sink.result;
        }
        first_ifd = off32;
    } else {
        if (tiff_bytes.size() < 16) {
            sink.result.status = ExifDecodeStatus::Malformed;
            return sink.result;
        }
        uint16_t off_size = 0;
        uint16_t reserved = 0;
        if (!read_tiff_u16(cfg, tiff_bytes, 4, &off_size)
            || !read_tiff_u16(cfg, tiff_bytes, 6, &reserved)) {
            sink.result.status = ExifDecodeStatus::Malformed;
            return sink.result;
        }
        if (off_size != 8 || reserved != 0) {
            sink.result.status = ExifDecodeStatus::Malformed;
            return sink.result;
        }
        if (!read_tiff_u64(cfg, tiff_bytes, 8, &first_ifd)) {
            sink.result.status = ExifDecodeStatus::Malformed;
            return sink.result;
        }
    }

    std::array<IfdTask, 256> stack_buf {};
    std::array<uint64_t, 256> visited_offs {};
    std::array<uint8_t, 256> visited_masks {};
    uint32_t stack_size        = 0;
    uint32_t visited_count     = 0;
    uint32_t next_subifd_index = 0;

    if (first_ifd != 0) {
        stack_buf[0] = IfdTask { ExifIfdKind::Ifd, 0, first_ifd };
        stack_size   = 1;
    }

    while (stack_size > 0) {
        const uint32_t next_index = select_next_task_index(
            std::span<const IfdTask>(stack_buf), stack_size);
        IfdTask task = stack_buf[next_index];
        stack_buf[next_index] = stack_buf[stack_size - 1];
        stack_size -= 1;

        if (task.offset == 0 || task.offset >= tiff_bytes.size()) {
            continue;
        }

        const uint8_t kind_bit = ifd_kind_bit(task.kind);
        const uint32_t vi
            = find_visited(task.offset, std::span<const uint64_t>(visited_offs),
                           visited_count);
        if (vi != 0xffffffffU) {
            const uint8_t mask = visited_masks[vi];
            if ((mask & kind_bit) != 0) {
                continue;
            }
            if (!allow_revisit_kind(task.kind, mask)) {
                continue;
            }
            visited_masks[vi] = static_cast<uint8_t>(mask | kind_bit);
        } else {
            if (visited_count < visited_offs.size()) {
                visited_offs[visited_count]  = task.offset;
                visited_masks[visited_count] = kind_bit;
                visited_count += 1;
            } else {
                update_status(&sink.result, ExifDecodeStatus::LimitExceeded);
                break;
            }
        }

        if (sink.result.ifds_needed >= options.limits.max_ifds) {
            update_status(&sink.result, ExifDecodeStatus::LimitExceeded);
            break;
        }

        uint64_t entry_count      = 0;
        uint64_t entries_off      = 0;
        uint64_t entry_size       = 0;
        uint64_t next_ifd_off_pos = 0;

        if (!cfg.bigtiff) {
            uint16_t n16 = 0;
            if (!read_tiff_u16(cfg, tiff_bytes, task.offset, &n16)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }
            entry_count      = n16;
            entries_off      = task.offset + 2;
            entry_size       = 12;
            next_ifd_off_pos = entries_off + entry_count * entry_size;
            if (task.kind == ExifIfdKind::Ifd) {
                if (next_ifd_off_pos + 4 <= tiff_bytes.size()) {
                    uint32_t next32 = 0;
                    if (read_tiff_u32(cfg, tiff_bytes, next_ifd_off_pos, &next32)
                        && next32 != 0) {
                        if (stack_size < stack_buf.size()
                            && stack_size < options.limits.max_ifds) {
                            stack_buf[stack_size] = IfdTask { ExifIfdKind::Ifd,
                                                              task.index + 1,
                                                              next32 };
                            stack_size += 1;
                        } else {
                            update_status(&sink.result,
                                          ExifDecodeStatus::LimitExceeded);
                        }
                    }
                } else {
                    // Truncated next-IFD pointer field. Decode entries anyway.
                    update_status(&sink.result, ExifDecodeStatus::Malformed);
                }
            }
        } else {
            uint64_t n64 = 0;
            if (!read_tiff_u64(cfg, tiff_bytes, task.offset, &n64)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }
            entry_count      = n64;
            entries_off      = task.offset + 8;
            entry_size       = 20;
            next_ifd_off_pos = entries_off + entry_count * entry_size;
            if (task.kind == ExifIfdKind::Ifd) {
                if (next_ifd_off_pos + 8 <= tiff_bytes.size()) {
                    uint64_t next64 = 0;
                    if (read_tiff_u64(cfg, tiff_bytes, next_ifd_off_pos, &next64)
                        && next64 != 0) {
                        if (stack_size < stack_buf.size()
                            && stack_size < options.limits.max_ifds) {
                            stack_buf[stack_size] = IfdTask { ExifIfdKind::Ifd,
                                                              task.index + 1,
                                                              next64 };
                            stack_size += 1;
                        } else {
                            update_status(&sink.result,
                                          ExifDecodeStatus::LimitExceeded);
                        }
                    }
                } else {
                    // Truncated next-IFD pointer field. Decode entries anyway.
                    update_status(&sink.result, ExifDecodeStatus::Malformed);
                }
            }
        }

        if (entry_count > options.limits.max_entries_per_ifd) {
            update_status(&sink.result, ExifDecodeStatus::LimitExceeded);
            continue;
        }
        if (entries_off + entry_count * entry_size > tiff_bytes.size()) {
            update_status(&sink.result, ExifDecodeStatus::Malformed);
            continue;
        }
        if (sink.result.entries_decoded + entry_count
            > options.limits.max_total_entries) {
            update_status(&sink.result, ExifDecodeStatus::LimitExceeded);
            continue;
        }

        BlockId block = store.add_block(BlockInfo {});
        ExifIfdRef ref;
        ref.kind   = task.kind;
        ref.index  = task.index;
        ref.offset = task.offset;
        ref.block  = block;
        sink_emit(&sink, ref);

        char token_scratch_buf[32];
        const std::string_view ifd_name
            = ifd_token(task.kind, task.index,
                        std::span<char>(token_scratch_buf));
        if (ifd_name.empty()) {
            update_status(&sink.result, ExifDecodeStatus::Malformed);
            continue;
        }

        for (uint64_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + i * entry_size;

            uint16_t tag  = 0;
            uint16_t type = 0;
            if (!read_tiff_u16(cfg, tiff_bytes, eoff + 0, &tag)
                || !read_tiff_u16(cfg, tiff_bytes, eoff + 2, &type)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }

            uint64_t count           = 0;
            uint64_t value_or_off    = 0;
            uint64_t value_field_off = 0;
            if (!cfg.bigtiff) {
                uint32_t c32 = 0;
                uint32_t v32 = 0;
                if (!read_tiff_u32(cfg, tiff_bytes, eoff + 4, &c32)
                    || !read_tiff_u32(cfg, tiff_bytes, eoff + 8, &v32)) {
                    update_status(&sink.result, ExifDecodeStatus::Malformed);
                    continue;
                }
                count           = c32;
                value_or_off    = v32;
                value_field_off = eoff + 8;
            } else {
                uint64_t c64 = 0;
                uint64_t v64 = 0;
                if (!read_tiff_u64(cfg, tiff_bytes, eoff + 4, &c64)
                    || !read_tiff_u64(cfg, tiff_bytes, eoff + 12, &v64)) {
                    update_status(&sink.result, ExifDecodeStatus::Malformed);
                    continue;
                }
                count           = c64;
                value_or_off    = v64;
                value_field_off = eoff + 12;
            }

            const uint64_t unit = tiff_type_size(type);
            if (unit == 0) {
                continue;
            }
            if (count > (UINT64_MAX / unit)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }
            const uint64_t value_bytes = count * unit;

            uint64_t value_off        = 0;
            const uint64_t inline_cap = cfg.bigtiff ? 8U : 4U;
            if (value_bytes <= inline_cap) {
                value_off = value_field_off;
            } else {
                value_off = value_or_off;
            }
            if (value_off + value_bytes > tiff_bytes.size()) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }

            (void)follow_ifd_pointers(cfg, tiff_bytes, tag, type, count,
                                      value_off, std::span<IfdTask>(stack_buf),
                                      &stack_size, &next_subifd_index,
                                      options.limits, &sink.result);

            if (count > UINT32_MAX) {
                update_status(&sink.result, ExifDecodeStatus::LimitExceeded);
                continue;
            }

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = static_cast<uint32_t>(i);
            entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
            entry.origin.wire_count     = static_cast<uint32_t>(count);
            entry.value = decode_tiff_value(cfg, tiff_bytes, type, count,
                                            value_off, value_bytes,
                                            store.arena(), options.limits,
                                            &sink.result);

            if (!options.include_pointer_tags
                && (tag == 0x8769 || tag == 0x8825 || tag == 0xA005
                    || tag == 0x014A)) {
                continue;
            }

            (void)store.add_entry(entry);
            sink.result.entries_decoded += 1;
        }
    }

    return sink.result;
}

}  // namespace openmeta
