// SPDX-License-Identifier: Apache-2.0

#include "openmeta/libraw_adapter.h"

#include "openmeta/meta_key.h"

#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static constexpr std::string_view kXmpNsTiff
        = "http://ns.adobe.com/tiff/1.0/";

    static bool is_mirrored_exif_orientation(uint16_t orientation) noexcept
    {
        switch (orientation) {
        case 2U:
        case 4U:
        case 5U:
        case 7U: return true;
        default: return false;
        }
    }

    static uint16_t
    drop_mirror_from_exif_orientation(uint16_t orientation) noexcept
    {
        switch (orientation) {
        case 2U: return 1U;
        case 4U: return 3U;
        case 5U: return 8U;
        case 7U: return 6U;
        default: return orientation;
        }
    }

    static bool map_exact_exif_orientation_to_libraw_flip(
        uint16_t orientation, uint32_t* out_flip,
        bool* out_apply_flip) noexcept
    {
        if (!out_flip || !out_apply_flip) {
            return false;
        }
        switch (orientation) {
        case 1U:
            *out_flip       = 0U;
            *out_apply_flip = false;
            return true;
        case 3U:
            *out_flip       = 3U;
            *out_apply_flip = true;
            return true;
        case 6U:
            *out_flip       = 6U;
            *out_apply_flip = true;
            return true;
        case 8U:
            *out_flip       = 5U;
            *out_apply_flip = true;
            return true;
        default: return false;
        }
    }

    static bool map_exact_libraw_flip_to_exif_orientation(
        uint32_t libraw_flip, uint16_t* out_orientation) noexcept
    {
        if (!out_orientation) {
            return false;
        }
        switch (libraw_flip) {
        case 0U:
            *out_orientation = 1U;
            return true;
        case 3U:
            *out_orientation = 3U;
            return true;
        case 5U:
            *out_orientation = 8U;
            return true;
        case 6U:
            *out_orientation = 6U;
            return true;
        default: return false;
        }
    }

    static bool exif_ifd_is_ifd0(const ByteArena& arena,
                                 const Entry& entry) noexcept
    {
        if (entry.key.kind != MetaKeyKind::ExifTag) {
            return false;
        }
        const std::span<const std::byte> ifd_bytes = arena.span(
            entry.key.data.exif_tag.ifd);
        const std::string_view ifd(reinterpret_cast<const char*>(
                                       ifd_bytes.data()),
                                   ifd_bytes.size());
        return ifd == "ifd0";
    }

    static bool entry_is_exif_orientation(const ByteArena& arena,
                                          const Entry& entry) noexcept
    {
        return entry.key.kind == MetaKeyKind::ExifTag
               && entry.key.data.exif_tag.tag == 0x0112U
               && exif_ifd_is_ifd0(arena, entry);
    }

    static bool entry_is_xmp_tiff_orientation(const ByteArena& arena,
                                              const Entry& entry) noexcept
    {
        if (entry.key.kind != MetaKeyKind::XmpProperty) {
            return false;
        }
        const std::span<const std::byte> ns_bytes = arena.span(
            entry.key.data.xmp_property.schema_ns);
        const std::span<const std::byte> path_bytes = arena.span(
            entry.key.data.xmp_property.property_path);
        const std::string_view schema_ns(reinterpret_cast<const char*>(
                                             ns_bytes.data()),
                                         ns_bytes.size());
        const std::string_view path(reinterpret_cast<const char*>(
                                        path_bytes.data()),
                                    path_bytes.size());
        return schema_ns == kXmpNsTiff && path == "Orientation";
    }

    static bool parse_decimal_u16_text(std::string_view text,
                                       uint16_t* out) noexcept
    {
        if (!out || text.empty()) {
            return false;
        }
        uint32_t value = 0U;
        for (size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c < '0' || c > '9') {
                return false;
            }
            value = value * 10U + static_cast<uint32_t>(c - '0');
            if (value > 65535U) {
                return false;
            }
        }
        *out = static_cast<uint16_t>(value);
        return true;
    }

    static bool extract_orientation_u16(const ByteArena& arena,
                                        const MetaValue& value,
                                        uint16_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (value.kind == MetaValueKind::Scalar && value.count == 1U) {
            switch (value.elem_type) {
            case MetaElementType::U8:
            case MetaElementType::U16:
            case MetaElementType::U32:
            case MetaElementType::U64:
                if (value.data.u64 > 65535ULL) {
                    return false;
                }
                *out = static_cast<uint16_t>(value.data.u64);
                return true;
            case MetaElementType::I8:
            case MetaElementType::I16:
            case MetaElementType::I32:
            case MetaElementType::I64:
                if (value.data.i64 < 0 || value.data.i64 > 65535LL) {
                    return false;
                }
                *out = static_cast<uint16_t>(value.data.i64);
                return true;
            default: return false;
            }
        }
        if (value.kind == MetaValueKind::Text) {
            const std::span<const std::byte> text_bytes = arena.span(
                value.data.span);
            const std::string_view text(reinterpret_cast<const char*>(
                                            text_bytes.data()),
                                        text_bytes.size());
            return parse_decimal_u16_text(text, out);
        }
        return false;
    }

    static bool extract_exif_orientation_from_store(const MetaStore& store,
                                                    uint16_t* out,
                                                    bool* found_invalid) noexcept
    {
        if (!out || !found_invalid) {
            return false;
        }
        *found_invalid = false;
        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (!entry_is_exif_orientation(store.arena(), entry)) {
                continue;
            }
            uint16_t orientation = 0U;
            if (!extract_orientation_u16(store.arena(), entry.value,
                                         &orientation)) {
                *found_invalid = true;
                continue;
            }
            *out = orientation;
            return true;
        }
        return false;
    }

    static bool extract_xmp_tiff_orientation_from_store(
        const MetaStore& store, uint16_t* out, bool* found_invalid) noexcept
    {
        if (!out || !found_invalid) {
            return false;
        }
        *found_invalid = false;
        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (!entry_is_xmp_tiff_orientation(store.arena(), entry)) {
                continue;
            }
            uint16_t orientation = 0U;
            if (!extract_orientation_u16(store.arena(), entry.value,
                                         &orientation)) {
                *found_invalid = true;
                continue;
            }
            *out = orientation;
            return true;
        }
        return false;
    }

    static bool has_read_failure(const SimpleMetaResult& read) noexcept
    {
        if (read.scan.status == ScanStatus::Malformed
            || read.payload.status == PayloadStatus::Malformed
            || read.payload.status == PayloadStatus::LimitExceeded
            || read.exif.status == ExifDecodeStatus::Malformed
            || read.exif.status == ExifDecodeStatus::LimitExceeded
            || read.xmp.status == XmpDecodeStatus::Malformed
            || read.xmp.status == XmpDecodeStatus::LimitExceeded
            || read.jumbf.status == JumbfDecodeStatus::Malformed
            || read.jumbf.status == JumbfDecodeStatus::LimitExceeded) {
            return true;
        }
        return false;
    }

    static LibRawOrientationFileStatus
    map_file_status(MappedFileStatus status) noexcept
    {
        switch (status) {
        case MappedFileStatus::Ok: return LibRawOrientationFileStatus::Ok;
        case MappedFileStatus::OpenFailed:
            return LibRawOrientationFileStatus::OpenFailed;
        case MappedFileStatus::StatFailed:
            return LibRawOrientationFileStatus::StatFailed;
        case MappedFileStatus::TooLarge:
            return LibRawOrientationFileStatus::TooLarge;
        case MappedFileStatus::MapFailed:
            return LibRawOrientationFileStatus::MapFailed;
        }
        return LibRawOrientationFileStatus::MapFailed;
    }

}  // namespace

LibRawOrientationResult
map_exif_orientation_to_libraw_flip(
    uint16_t exif_orientation, const LibRawOrientationOptions& options) noexcept
{
    LibRawOrientationResult result {};
    result.exif_orientation = exif_orientation;

    if (exif_orientation < 1U || exif_orientation > 8U) {
        result.status = LibRawOrientationStatus::InvalidArgument;
        result.code   = LibRawOrientationCode::InvalidExifOrientation;
        return result;
    }

    if (options.target == LibRawOrientationTarget::EmbeddedPreview
        && options.preserve_embedded_preview_orientation) {
        result.code               = LibRawOrientationCode::PreviewPassThrough;
        result.preview_passthrough = true;
        return result;
    }

    uint16_t effective_orientation = exif_orientation;
    if (is_mirrored_exif_orientation(exif_orientation)) {
        result.mirrored = true;
        if (options.mirror_policy == LibRawMirrorPolicy::Reject) {
            result.status = LibRawOrientationStatus::Unsupported;
            result.code   = LibRawOrientationCode::UnsupportedMirroredOrientation;
            return result;
        }
        effective_orientation = drop_mirror_from_exif_orientation(
            exif_orientation);
        result.code = LibRawOrientationCode::MirroredOrientationDropped;
    }

    if (!map_exact_exif_orientation_to_libraw_flip(
            effective_orientation, &result.libraw_flip,
            &result.apply_flip)) {
        result.status = LibRawOrientationStatus::Unsupported;
        if (result.code == LibRawOrientationCode::None) {
            result.code = LibRawOrientationCode::UnsupportedMirroredOrientation;
        }
        return result;
    }

    return result;
}

LibRawFlipToExifResult
map_libraw_flip_to_exif_orientation(
    uint32_t libraw_flip, const LibRawFlipToExifOptions& options) noexcept
{
    LibRawFlipToExifResult result {};
    result.libraw_flip = libraw_flip;

    if (options.target == LibRawOrientationTarget::EmbeddedPreview
        && options.preserve_embedded_preview_orientation) {
        result.code               = LibRawFlipToExifCode::PreviewPassThrough;
        result.preview_passthrough = true;
        result.exif_orientation   = 1U;
        return result;
    }

    if (!map_exact_libraw_flip_to_exif_orientation(libraw_flip,
                                                   &result.exif_orientation)) {
        result.status = LibRawOrientationStatus::InvalidArgument;
        result.code   = LibRawFlipToExifCode::InvalidLibRawFlip;
        return result;
    }

    return result;
}

LibRawOrientationResult
map_meta_orientation_to_libraw_flip(
    const MetaStore& store, const LibRawOrientationOptions& options) noexcept
{
    uint16_t orientation = 0U;
    bool found_invalid   = false;
    if (extract_exif_orientation_from_store(store, &orientation,
                                            &found_invalid)) {
        LibRawOrientationResult result
            = map_exif_orientation_to_libraw_flip(orientation, options);
        result.source = LibRawOrientationSource::ExifIfd0;
        return result;
    }
    if (found_invalid) {
        LibRawOrientationResult result {};
        result.status = LibRawOrientationStatus::InvalidArgument;
        result.code   = LibRawOrientationCode::InvalidExifOrientation;
        result.source = LibRawOrientationSource::ExifIfd0;
        return result;
    }

    if (extract_xmp_tiff_orientation_from_store(store, &orientation,
                                                &found_invalid)) {
        LibRawOrientationResult result
            = map_exif_orientation_to_libraw_flip(orientation, options);
        result.source = LibRawOrientationSource::XmpTiffOrientation;
        return result;
    }
    if (found_invalid) {
        LibRawOrientationResult result {};
        result.status = LibRawOrientationStatus::InvalidArgument;
        result.code   = LibRawOrientationCode::InvalidExifOrientation;
        result.source = LibRawOrientationSource::XmpTiffOrientation;
        return result;
    }

    LibRawOrientationResult result
        = map_exif_orientation_to_libraw_flip(1U, options);
    result.code            = LibRawOrientationCode::MissingExifOrientationAssumedDefault;
    result.source          = LibRawOrientationSource::AssumedDefault;
    result.exif_orientation = 1U;
    return result;
}

LibRawOrientationFileResult
map_meta_orientation_to_libraw_flip_from_file(
    const char* path, const LibRawOrientationFileOptions& options) noexcept
{
    LibRawOrientationFileResult out;

    if (!path || !*path) {
        out.file_status        = LibRawOrientationFileStatus::InvalidArgument;
        out.orientation.status = LibRawOrientationStatus::InvalidArgument;
        out.orientation.code   = LibRawOrientationCode::InvalidExifOrientation;
        return out;
    }

    MappedFile mapped;
    out.mapped_file_status = mapped.open(path, options.max_file_bytes);
    if (out.mapped_file_status != MappedFileStatus::Ok) {
        out.file_status = map_file_status(out.mapped_file_status);
        return out;
    }
    out.file_size = mapped.size();

    std::vector<ContainerBlockRef> blocks(64U);
    std::vector<ExifIfdRef> ifds(128U);
    std::vector<std::byte> payload(1024U * 1024U);
    std::vector<uint32_t> payload_parts(4096U);
    MetaStore store;

    for (;;) {
        store = MetaStore();
        out.read = simple_meta_read(
            mapped.bytes(), store,
            std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
            std::span<ExifIfdRef>(ifds.data(), ifds.size()),
            std::span<std::byte>(payload.data(), payload.size()),
            std::span<uint32_t>(payload_parts.data(), payload_parts.size()),
            options.decode);

        bool retried = false;
        if (out.read.scan.status == ScanStatus::OutputTruncated
            && out.read.scan.needed > blocks.size()) {
            blocks.resize(out.read.scan.needed);
            retried = true;
        }
        if (out.read.exif.status == ExifDecodeStatus::OutputTruncated
            && out.read.exif.ifds_needed > ifds.size()) {
            ifds.resize(out.read.exif.ifds_needed);
            retried = true;
        }
        if (out.read.payload.status == PayloadStatus::OutputTruncated
            && out.read.payload.needed > payload.size()) {
            if (out.read.payload.needed > static_cast<uint64_t>(SIZE_MAX)) {
                out.file_status        = LibRawOrientationFileStatus::DecodeFailed;
                out.orientation.status = LibRawOrientationStatus::Unsupported;
                return out;
            }
            payload.resize(static_cast<size_t>(out.read.payload.needed));
            retried = true;
        }
        if (!retried) {
            break;
        }
    }

    if (has_read_failure(out.read)) {
        out.file_status        = LibRawOrientationFileStatus::DecodeFailed;
        out.orientation.status = LibRawOrientationStatus::Unsupported;
        return out;
    }

    out.orientation = map_meta_orientation_to_libraw_flip(
        store, options.orientation);
    out.file_status = LibRawOrientationFileStatus::Ok;
    return out;
}

}  // namespace openmeta
