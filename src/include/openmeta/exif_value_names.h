// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string_view>

/**
 * \file exif_value_names.h
 * \brief Human-readable names for common EXIF/TIFF/DNG numeric values and
 * selected bounded MakerNote contexts.
 */

namespace openmeta {

const char*
tiff_compression_name(uint64_t value) noexcept;

const char*
tiff_photometric_interpretation_name(uint64_t value) noexcept;

const char*
tiff_planar_configuration_name(uint64_t value) noexcept;

const char*
tiff_resolution_unit_name(uint64_t value) noexcept;

const char*
exif_exposure_program_name(uint64_t value) noexcept;

const char*
exif_exposure_mode_name(uint64_t value) noexcept;

const char*
exif_metering_mode_name(uint64_t value) noexcept;

const char*
exif_light_source_name(uint64_t value) noexcept;

const char*
exif_flash_name(uint64_t value) noexcept;

const char*
exif_color_space_name(uint64_t value) noexcept;

const char*
exif_white_balance_name(uint64_t value) noexcept;

const char*
exif_scene_capture_type_name(uint64_t value) noexcept;

const char*
exif_gain_control_name(uint64_t value) noexcept;

const char*
dng_cfa_layout_name(uint64_t value) noexcept;

const char*
dng_calibration_illuminant_name(uint64_t value) noexcept;

/**
 * \brief Interprets common numeric enum-like values by EXIF/TIFF/DNG tag id
 * and selected bounded MakerNote contexts.
 *
 * Returns an empty string when OpenMeta has no stable public interpretation for
 * the value. Unknown values remain numeric and lossless in the underlying
 * MetaStore entry.
 */
const char*
exif_tag_numeric_value_name(std::string_view ifd, uint16_t tag,
                            uint64_t value) noexcept;

}  // namespace openmeta
