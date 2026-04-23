// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_capabilities.h"

#include <gtest/gtest.h>

TEST(MetadataCapabilities, NamesAreStable)
{
    EXPECT_STREQ(openmeta::metadata_capability_family_name(
                     openmeta::MetadataCapabilityFamily::Exif),
                 "exif");
    EXPECT_STREQ(openmeta::metadata_capability_family_name(
                     openmeta::MetadataCapabilityFamily::BmffFields),
                 "bmff_fields");
    EXPECT_STREQ(openmeta::metadata_capability_support_name(
                     openmeta::MetadataCapabilitySupport::Unsupported),
                 "unsupported");
    EXPECT_STREQ(openmeta::metadata_capability_support_name(
                     openmeta::MetadataCapabilitySupport::Supported),
                 "supported");
    EXPECT_STREQ(openmeta::metadata_capability_support_name(
                     openmeta::MetadataCapabilitySupport::Bounded),
                 "bounded");
    EXPECT_STREQ(openmeta::metadata_capability_support_name(
                     openmeta::MetadataCapabilitySupport::Disabled),
                 "disabled");
}

TEST(MetadataCapabilities, AvailabilityTreatsSupportedAndBoundedAsAvailable)
{
    EXPECT_FALSE(openmeta::metadata_capability_available(
        openmeta::MetadataCapabilitySupport::Unsupported));
    EXPECT_TRUE(openmeta::metadata_capability_available(
        openmeta::MetadataCapabilitySupport::Supported));
    EXPECT_TRUE(openmeta::metadata_capability_available(
        openmeta::MetadataCapabilitySupport::Bounded));
    EXPECT_FALSE(openmeta::metadata_capability_available(
        openmeta::MetadataCapabilitySupport::Disabled));
}

TEST(MetadataCapabilities, PrimaryStillExifEditIsSupported)
{
    const openmeta::MetadataCapability jpeg = openmeta::metadata_capability(
        openmeta::TransferTargetFormat::Jpeg,
        openmeta::MetadataCapabilityFamily::Exif);
    EXPECT_EQ(jpeg.read, openmeta::MetadataCapabilitySupport::Supported);
    EXPECT_EQ(jpeg.structured_decode,
              openmeta::MetadataCapabilitySupport::Supported);
    EXPECT_EQ(jpeg.transfer_prepare,
              openmeta::MetadataCapabilitySupport::Supported);
    EXPECT_EQ(jpeg.target_edit,
              openmeta::MetadataCapabilitySupport::Supported);

    const openmeta::MetadataCapability avif = openmeta::metadata_capability(
        openmeta::TransferTargetFormat::Avif,
        openmeta::MetadataCapabilityFamily::Exif);
    EXPECT_EQ(avif.read, openmeta::MetadataCapabilitySupport::Supported);
    EXPECT_EQ(avif.target_edit,
              openmeta::MetadataCapabilitySupport::Bounded);
}

TEST(MetadataCapabilities, XmpReflectsBuildConfiguration)
{
    const openmeta::MetadataCapability xmp = openmeta::metadata_capability(
        openmeta::TransferTargetFormat::Jxl,
        openmeta::MetadataCapabilityFamily::Xmp);
#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT
    EXPECT_EQ(xmp.read, openmeta::MetadataCapabilitySupport::Supported);
    EXPECT_EQ(xmp.structured_decode,
              openmeta::MetadataCapabilitySupport::Supported);
#else
    EXPECT_EQ(xmp.read, openmeta::MetadataCapabilitySupport::Disabled);
    EXPECT_EQ(xmp.structured_decode,
              openmeta::MetadataCapabilitySupport::Disabled);
#endif
}

TEST(MetadataCapabilities, BmffFieldsAreReadOnlyContainerMetadata)
{
    const openmeta::MetadataCapability bmff = openmeta::metadata_capability(
        openmeta::TransferTargetFormat::Cr3,
        openmeta::MetadataCapabilityFamily::BmffFields);
    EXPECT_EQ(bmff.read, openmeta::MetadataCapabilitySupport::Supported);
    EXPECT_EQ(bmff.structured_decode,
              openmeta::MetadataCapabilitySupport::Supported);
    EXPECT_EQ(bmff.transfer_prepare,
              openmeta::MetadataCapabilitySupport::Unsupported);
    EXPECT_EQ(bmff.target_edit,
              openmeta::MetadataCapabilitySupport::Unsupported);

    const openmeta::MetadataCapability jpeg = openmeta::metadata_capability(
        openmeta::TransferTargetFormat::Jpeg,
        openmeta::MetadataCapabilityFamily::BmffFields);
    EXPECT_EQ(jpeg.read, openmeta::MetadataCapabilitySupport::Unsupported);
}

TEST(MetadataCapabilities, ExrAttributesAreScopedToExr)
{
    const openmeta::MetadataCapability exr = openmeta::metadata_capability(
        openmeta::TransferTargetFormat::Exr,
        openmeta::MetadataCapabilityFamily::ExrAttribute);
    EXPECT_EQ(exr.read, openmeta::MetadataCapabilitySupport::Supported);
    EXPECT_EQ(exr.transfer_prepare,
              openmeta::MetadataCapabilitySupport::Bounded);
    EXPECT_EQ(exr.target_edit,
              openmeta::MetadataCapabilitySupport::Bounded);

    const openmeta::MetadataCapability jpeg = openmeta::metadata_capability(
        openmeta::TransferTargetFormat::Jpeg,
        openmeta::MetadataCapabilityFamily::ExrAttribute);
    EXPECT_EQ(jpeg.read, openmeta::MetadataCapabilitySupport::Unsupported);
}

TEST(MetadataCapabilities, IptcDistinguishesNativeCarrierFromProjection)
{
    const openmeta::MetadataCapability jp2 = openmeta::metadata_capability(
        openmeta::TransferTargetFormat::Jp2,
        openmeta::MetadataCapabilityFamily::Iptc);
    EXPECT_EQ(jp2.read, openmeta::MetadataCapabilitySupport::Supported);
    EXPECT_EQ(jp2.raw_preservation,
              openmeta::MetadataCapabilitySupport::Bounded);

    const openmeta::MetadataCapability png = openmeta::metadata_capability(
        openmeta::TransferTargetFormat::Png,
        openmeta::MetadataCapabilityFamily::Iptc);
    EXPECT_EQ(png.read, openmeta::MetadataCapabilitySupport::Unsupported);
    EXPECT_EQ(png.transfer_prepare,
              openmeta::MetadataCapabilitySupport::Bounded);
    EXPECT_EQ(png.raw_preservation,
              openmeta::MetadataCapabilitySupport::Unsupported);
}
