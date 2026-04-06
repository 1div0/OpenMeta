// SPDX-License-Identifier: Apache-2.0

#include "openmeta/dng_sdk_adapter.h"

#include "openmeta/meta_key.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/meta_value.h"

#include <gtest/gtest.h>

#include <string>

#if defined(OPENMETA_HAS_DNG_SDK) && OPENMETA_HAS_DNG_SDK
#    include "dng_auto_ptr.h"
#    include "dng_exif.h"
#    include "dng_host.h"
#    include "dng_negative.h"
#endif

namespace {

static openmeta::PreparedTransferBundle
make_prepared_dng_bundle()
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});

    openmeta::Entry make;
    make.key = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x010FU);
    make.value = openmeta::make_text(store.arena(), "Vendor",
                                     openmeta::TextEncoding::Utf8);
    make.origin.block          = block;
    make.origin.order_in_block = 0U;
    EXPECT_NE(store.add_entry(make), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(store.arena(), "OpenMeta",
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1U;
    EXPECT_NE(store.add_entry(xmp), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Dng;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    EXPECT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    return bundle;
}

}  // namespace

TEST(DngSdkAdapter, ReportsAvailabilityFlag)
{
#if defined(OPENMETA_HAS_DNG_SDK) && OPENMETA_HAS_DNG_SDK
    EXPECT_TRUE(openmeta::dng_sdk_adapter_available());
#else
    EXPECT_FALSE(openmeta::dng_sdk_adapter_available());
#endif
}

TEST(DngSdkAdapter, ReturnsUnsupportedWhenSdkUnavailable)
{
#if !defined(OPENMETA_HAS_DNG_SDK) || !OPENMETA_HAS_DNG_SDK
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Dng;
    const openmeta::DngSdkAdapterResult result
        = openmeta::apply_prepared_dng_sdk_metadata(bundle, nullptr, nullptr);
    EXPECT_EQ(result.status, openmeta::DngSdkAdapterStatus::Unsupported);
    EXPECT_FALSE(result.message.empty());
#endif
}

#if defined(OPENMETA_HAS_DNG_SDK) && OPENMETA_HAS_DNG_SDK
TEST(DngSdkAdapter, AppliesPreparedExifAndXmpToNegative)
{
    const openmeta::PreparedTransferBundle bundle = make_prepared_dng_bundle();

    ::dng_host host;
    AutoPtr<dng_negative> negative(host.Make_dng_negative());

    const openmeta::DngSdkAdapterResult result
        = openmeta::apply_prepared_dng_sdk_metadata(bundle, &host,
                                                    negative.Get());

    ASSERT_EQ(result.status, openmeta::DngSdkAdapterStatus::Ok);
    EXPECT_TRUE(result.exif_applied);
    EXPECT_TRUE(result.xmp_applied);
    EXPECT_TRUE(result.synchronized_metadata);
    ASSERT_NE(negative->GetExif(), nullptr);
    EXPECT_STREQ(negative->GetExif()->fMake.Get(), "Vendor");
}
#endif
