#pragma once

#include "openmeta/meta_store.h"

#include <cstdint>
#include <string_view>

/**
 * \file interop_export.h
 * \brief Draft metadata export traversal API for interop adapters.
 */

namespace openmeta {

/**
 * \brief Key naming policy used by \ref visit_metadata.
 */
enum class ExportNameStyle : uint8_t {
    /// Stable, key-space-aware names (for example: `exif:ifd0:0x010F`).
    Canonical,
    /// Portable XMP-like names (for example: `tiff:Make`, `exif:ExposureTime`).
    XmpPortable,
    /// OIIO-style names (for example: `Make`, `Exif:ExposureTime`).
    Oiio,
};

/**
 * \brief Export controls for \ref visit_metadata.
 *
 * \note This API is marked draft and may change.
 */
struct ExportOptions final {
    ExportNameStyle style   = ExportNameStyle::Canonical;
    bool include_origin     = false;
    bool include_flags      = false;
    bool include_makernotes = true;
};

/**
 * \brief A single exported metadata item.
 *
 * The \ref name view is valid only for the duration of \ref MetadataSink::on_item.
 */
struct ExportItem final {
    std::string_view name;
    const Entry* entry   = nullptr;
    const Origin* origin = nullptr;
    EntryFlags flags     = EntryFlags::None;
};

class MetadataSink {
public:
    virtual ~MetadataSink()                      = default;
    virtual void on_item(const ExportItem& item) = 0;
};

/**
 * \brief Visits exported metadata entries in store order.
 *
 * Deleted entries are skipped. Name mapping depends on \ref ExportOptions::style.
 */
void
visit_metadata(const MetaStore& store, const ExportOptions& options,
               MetadataSink& sink) noexcept;

}  // namespace openmeta
