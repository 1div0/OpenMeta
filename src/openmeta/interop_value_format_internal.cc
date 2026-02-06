#include "interop_value_format_internal.h"

#include "openmeta/console_format.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace openmeta::interop_internal {
namespace {

    static uint32_t meta_element_size(MetaElementType type) noexcept
    {
        switch (type) {
        case MetaElementType::U8:
        case MetaElementType::I8: return 1U;
        case MetaElementType::U16:
        case MetaElementType::I16: return 2U;
        case MetaElementType::U32:
        case MetaElementType::I32:
        case MetaElementType::F32: return 4U;
        case MetaElementType::U64:
        case MetaElementType::I64:
        case MetaElementType::F64: return 8U;
        case MetaElementType::URational:
        case MetaElementType::SRational: return 8U;
        }
        return 0U;
    }


    static void append_u64_dec(uint64_t v, std::string* out) noexcept
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llu",
                      static_cast<unsigned long long>(v));
        out->append(buf);
    }


    static void append_i64_dec(int64_t v, std::string* out) noexcept
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        out->append(buf);
    }


    static void append_f64_dec(double v, std::string* out) noexcept
    {
        if (!std::isfinite(v)) {
            out->append("0");
            return;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", v);
        out->append(buf);
    }


    static void append_urational_text(const URational& r,
                                      std::string* out) noexcept
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%u/%u", static_cast<unsigned>(r.numer),
                      static_cast<unsigned>(r.denom));
        out->append(buf);
    }


    static void append_srational_text(const SRational& r,
                                      std::string* out) noexcept
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d/%d", static_cast<int>(r.numer),
                      static_cast<int>(r.denom));
        out->append(buf);
    }


    static bool append_scalar_text(const MetaValue& value,
                                   std::string* out) noexcept
    {
        switch (value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64:
            append_u64_dec(value.data.u64, out);
            return true;
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64:
            append_i64_dec(value.data.i64, out);
            return true;
        case MetaElementType::F32: {
            float f = 0.0f;
            std::memcpy(&f, &value.data.f32_bits, sizeof(f));
            append_f64_dec(static_cast<double>(f), out);
            return true;
        }
        case MetaElementType::F64: {
            double d = 0.0;
            std::memcpy(&d, &value.data.f64_bits, sizeof(d));
            append_f64_dec(d, out);
            return true;
        }
        case MetaElementType::URational:
            append_urational_text(value.data.ur, out);
            return true;
        case MetaElementType::SRational:
            append_srational_text(value.data.sr, out);
            return true;
        }
        return false;
    }


    static bool append_array_element_text(std::span<const std::byte> raw,
                                          size_t offset,
                                          MetaElementType elem_type,
                                          std::string* out) noexcept
    {
        switch (elem_type) {
        case MetaElementType::U8: {
            append_u64_dec(static_cast<uint8_t>(raw[offset]), out);
            return true;
        }
        case MetaElementType::I8: {
            const int8_t v = static_cast<int8_t>(
                static_cast<uint8_t>(raw[offset]));
            append_i64_dec(v, out);
            return true;
        }
        case MetaElementType::U16: {
            uint16_t v = 0;
            std::memcpy(&v, raw.data() + offset, sizeof(v));
            append_u64_dec(v, out);
            return true;
        }
        case MetaElementType::I16: {
            int16_t v = 0;
            std::memcpy(&v, raw.data() + offset, sizeof(v));
            append_i64_dec(v, out);
            return true;
        }
        case MetaElementType::U32: {
            uint32_t v = 0;
            std::memcpy(&v, raw.data() + offset, sizeof(v));
            append_u64_dec(v, out);
            return true;
        }
        case MetaElementType::I32: {
            int32_t v = 0;
            std::memcpy(&v, raw.data() + offset, sizeof(v));
            append_i64_dec(v, out);
            return true;
        }
        case MetaElementType::U64: {
            uint64_t v = 0;
            std::memcpy(&v, raw.data() + offset, sizeof(v));
            append_u64_dec(v, out);
            return true;
        }
        case MetaElementType::I64: {
            int64_t v = 0;
            std::memcpy(&v, raw.data() + offset, sizeof(v));
            append_i64_dec(v, out);
            return true;
        }
        case MetaElementType::F32: {
            uint32_t bits = 0;
            std::memcpy(&bits, raw.data() + offset, sizeof(bits));
            float f = 0.0f;
            std::memcpy(&f, &bits, sizeof(f));
            append_f64_dec(static_cast<double>(f), out);
            return true;
        }
        case MetaElementType::F64: {
            uint64_t bits = 0;
            std::memcpy(&bits, raw.data() + offset, sizeof(bits));
            double d = 0.0;
            std::memcpy(&d, &bits, sizeof(d));
            append_f64_dec(d, out);
            return true;
        }
        case MetaElementType::URational: {
            URational r;
            std::memcpy(&r, raw.data() + offset, sizeof(r));
            append_urational_text(r, out);
            return true;
        }
        case MetaElementType::SRational: {
            SRational r;
            std::memcpy(&r, raw.data() + offset, sizeof(r));
            append_srational_text(r, out);
            return true;
        }
        }
        return false;
    }


    static bool append_array_text(const ByteArena& arena,
                                  const MetaValue& value,
                                  uint32_t max_value_bytes,
                                  std::string* out) noexcept
    {
        const std::span<const std::byte> raw = arena.span(value.data.span);
        const uint32_t elem_size = meta_element_size(value.elem_type);
        if (elem_size == 0U) {
            return false;
        }

        uint32_t count            = value.count;
        const uint32_t max_by_raw = static_cast<uint32_t>(raw.size()
                                                          / elem_size);
        if (count > max_by_raw) {
            count = max_by_raw;
        }
        if (count == 0U) {
            out->append("[]");
            return true;
        }

        uint32_t max_elements = count;
        if (max_value_bytes != 0U) {
            const uint32_t bounded = max_value_bytes / 8U;
            if (bounded == 0U) {
                max_elements = 1U;
            } else if (max_elements > bounded) {
                max_elements = bounded;
            }
        } else if (max_elements > 2048U) {
            max_elements = 2048U;
        }

        out->push_back('[');
        for (uint32_t i = 0; i < max_elements; ++i) {
            if (i != 0U) {
                out->append(", ");
            }
            const size_t off = static_cast<size_t>(i) * elem_size;
            if (!append_array_element_text(raw, off, value.elem_type, out)) {
                return false;
            }
        }
        if (max_elements < count) {
            if (max_elements != 0U) {
                out->append(", ");
            }
            out->append("...");
        }
        out->push_back(']');
        return true;
    }

}  // namespace


bool
format_value_for_text(const ByteArena& arena, const MetaValue& value,
                      uint32_t max_value_bytes, std::string* out) noexcept
{
    if (!out) {
        return false;
    }
    out->clear();

    switch (value.kind) {
    case MetaValueKind::Empty: return false;
    case MetaValueKind::Scalar: return append_scalar_text(value, out);
    case MetaValueKind::Text: {
        const std::span<const std::byte> bytes = arena.span(value.data.span);
        const std::string_view s(reinterpret_cast<const char*>(bytes.data()),
                                 bytes.size());
        (void)append_console_escaped_ascii(s, max_value_bytes, out);
        return true;
    }
    case MetaValueKind::Bytes: {
        const std::span<const std::byte> bytes = arena.span(value.data.span);
        out->append("0x");
        append_hex_bytes(bytes, max_value_bytes, out);
        return true;
    }
    case MetaValueKind::Array:
        return append_array_text(arena, value, max_value_bytes, out);
    }
    return false;
}

}  // namespace openmeta::interop_internal
