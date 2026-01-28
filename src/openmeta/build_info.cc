#include "openmeta/build_info.h"

#include "openmeta/build_info_generated.h"

namespace openmeta {
namespace {

    static constexpr bool linkage_static() noexcept
    {
#if defined(OPENMETA_BUILD_LINKAGE_STATIC) && OPENMETA_BUILD_LINKAGE_STATIC
        return true;
#else
        return false;
#endif
    }

    static constexpr bool linkage_shared() noexcept
    {
#if defined(OPENMETA_BUILD_LINKAGE_SHARED) && OPENMETA_BUILD_LINKAGE_SHARED
        return true;
#else
        return false;
#endif
    }

    static constexpr bool has_zlib() noexcept
    {
#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
        return true;
#else
        return false;
#endif
    }

    static constexpr bool has_brotli() noexcept
    {
#if defined(OPENMETA_HAS_BROTLI) && OPENMETA_HAS_BROTLI
        return true;
#else
        return false;
#endif
    }

    static constexpr BuildInfo kBuildInfo = {
        /*version=*/OPENMETA_BUILDINFO_VERSION,
        /*build_timestamp_utc=*/OPENMETA_BUILDINFO_BUILD_TIMESTAMP_UTC,
        /*build_type=*/OPENMETA_BUILDINFO_BUILD_TYPE,
        /*cmake_generator=*/OPENMETA_BUILDINFO_CMAKE_GENERATOR,
        /*system_name=*/OPENMETA_BUILDINFO_SYSTEM_NAME,
        /*system_processor=*/OPENMETA_BUILDINFO_SYSTEM_PROCESSOR,
        /*cxx_compiler_id=*/OPENMETA_BUILDINFO_CXX_COMPILER_ID,
        /*cxx_compiler_version=*/OPENMETA_BUILDINFO_CXX_COMPILER_VERSION,
        /*cxx_compiler=*/OPENMETA_BUILDINFO_CXX_COMPILER,
        /*linkage_static=*/linkage_static(),
        /*linkage_shared=*/linkage_shared(),
        /*option_with_zlib=*/static_cast<bool>(OPENMETA_BUILDINFO_WITH_ZLIB),
        /*option_with_brotli=*/static_cast<bool>(OPENMETA_BUILDINFO_WITH_BROTLI),
        /*has_zlib=*/has_zlib(),
        /*has_brotli=*/has_brotli(),
    };

}  // namespace

const BuildInfo&
build_info() noexcept
{
    return kBuildInfo;
}

}  // namespace openmeta
