#include "openmeta/build_info.h"

#include "openmeta/build_info_generated.h"

#include <string>

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

namespace {

    static const char* linkage_string(const BuildInfo& bi) noexcept
    {
        if (bi.linkage_static) {
            return "static";
        }
        if (bi.linkage_shared) {
            return "shared";
        }
        return "unknown";
    }

    static void append_sv(std::string* out, std::string_view s) noexcept
    {
        if (!out) {
            return;
        }
        out->append(s.data(), s.size());
    }

}  // namespace

void
format_build_info_lines(const BuildInfo& bi, std::string* line1,
                        std::string* line2) noexcept
{
    if (line1) {
        line1->clear();
        line1->reserve(128);
        line1->append("OpenMeta v");
        append_sv(line1, bi.version);
        line1->append(" ");
        append_sv(line1, bi.build_type);
        line1->append(" [");
        bool first = true;
        if (bi.has_zlib) {
            line1->append("zlib");
            first = false;
        }
        if (bi.has_brotli) {
            if (!first) {
                line1->append(",");
            }
            line1->append("brotli");
            first = false;
        }
        line1->append("] ");
        line1->append(linkage_string(bi));
    }

    if (line2) {
        line2->clear();
        line2->reserve(160);
        line2->append("built with ");
        append_sv(line2, bi.cxx_compiler_id);
        line2->append("-");
        append_sv(line2, bi.cxx_compiler_version);
        line2->append(" for ");
        append_sv(line2, bi.system_name);
        line2->append("/");
        append_sv(line2, bi.system_processor);

        if (!bi.build_timestamp_utc.empty()) {
            line2->append(" (");
            append_sv(line2, bi.build_timestamp_utc);
            line2->append(")");
        }
    }
}

void
format_build_info_lines(std::string* line1, std::string* line2) noexcept
{
    format_build_info_lines(build_info(), line1, line2);
}

}  // namespace openmeta
