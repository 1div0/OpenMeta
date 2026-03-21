#include <cstdint>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    struct MakerNoteTagNameEntry final {
        uint16_t tag     = 0;
        const char* name = nullptr;
    };

    struct MakerNoteTableMap final {
        const char* key                      = nullptr;
        const MakerNoteTagNameEntry* entries = nullptr;
        uint32_t count                       = 0;
    };

#include "exif_makernote_tag_names_generated.inc"

    static constexpr MakerNoteTagNameEntry kMakernoteNikoncustomSettingsd4Extra[]
        = {
              { 0x0000u, "CustomSettingsBank" },
              { 0x0001u, "AF-CPrioritySelection" },
              { 0x0002u, "AFActivation" },
              { 0x0004u, "Pitch" },
              { 0x0005u, "ShootingInfoDisplay" },
              { 0x0006u, "ReverseIndicators" },
              { 0x0007u, "ExposureControlStepSize" },
              { 0x0008u, "CenterWeightedAreaSize" },
              { 0x0009u, "FineTuneOptCenterWeighted" },
              { 0x000Au, "MultiSelectorShootMode" },
              { 0x000Bu, "ExposureDelayMode" },
              { 0x000Cu, "MaxContinuousRelease" },
              { 0x000Du, "AutoBracketSet" },
              { 0x000Eu, "FuncButton" },
              { 0x000Fu, "PreviewButton" },
              { 0x0010u, "AssignBktButton" },
              { 0x0012u, "CommandDialsChangeMainSub" },
              { 0x0013u, "StandbyTimer" },
              { 0x0014u, "SelfTimerTime" },
              { 0x0015u, "ImageReviewMonitorOffTime" },
              { 0x0016u, "MenuMonitorOffTime" },
              { 0x0017u, "FlashSyncSpeed" },
              { 0x001Fu, "ModelingFlash" },
              { 0x0024u, "PlaybackMonitorOffTime" },
              { 0x0025u, "PlaybackZoom" },
              { 0x0026u, "ShutterSpeedLock" },
              { 0x0029u, "MovieFunctionButton" },
              { 0x002Au, "VerticalMultiSelector" },
              { 0x002Bu, "VerticalFuncButtonPlusDials" },
              { 0x002Eu, "DynamicAreaAFDisplay" },
              { 0x002Fu, "AFOnButton" },
              { 0x0030u, "SubSelectorAssignment" },
              { 0x0031u, "SubSelector" },
              { 0x0032u, "MatrixMetering" },
              { 0x0033u, "LimitAFAreaModeSelection" },
              { 0x0034u, "MovieFunctionButtonPlusDials" },
              { 0x0035u, "MovieSubSelectorAssignmentPlusDials" },
              { 0x0036u, "AssignRemoteFnButton" },
              { 0x0037u, "LensFocusFunctionButtons" },
          };

    static constexpr MakerNoteTagNameEntry kMakernoteNikoncustomSettingsd5Extra[]
        = {
              { 0x0000u, "CustomSettingsBank" },
              { 0x0001u, "AF-CPrioritySelection" },
              { 0x0002u, "FocusPointWrap" },
              { 0x0004u, "ISODisplay" },
              { 0x0005u, "LCDIllumination" },
              { 0x0006u, "ReverseIndicators" },
              { 0x0007u, "ExposureControlStepSize" },
              { 0x0008u, "CenterWeightedAreaSize" },
              { 0x0009u, "FineTuneOptCenterWeighted" },
              { 0x000Au, "MultiSelectorShootMode" },
              { 0x000Bu, "ExposureDelayMode" },
              { 0x000Cu, "MaxContinuousRelease" },
              { 0x000Du, "AutoBracketOrder" },
              { 0x000Eu, "Func1Button" },
              { 0x000Fu, "PreviewButton" },
              { 0x0010u, "AssignBktButton" },
              { 0x0012u, "CommandDialsChangeMainSub" },
              { 0x0013u, "StandbyTimer" },
              { 0x0014u, "SelfTimerTime" },
              { 0x0015u, "ImageReviewMonitorOffTime" },
              { 0x0016u, "MenuMonitorOffTime" },
              { 0x0017u, "FlashSyncSpeed" },
              { 0x001Fu, "ModelingFlash" },
              { 0x0024u, "PlaybackMonitorOffTime" },
              { 0x0025u, "MultiSelectorLiveView" },
              { 0x0026u, "ShutterSpeedLock" },
              { 0x0029u, "MovieFunc1Button" },
              { 0x002Au, "Func1ButtonPlusDials" },
              { 0x002Bu, "PreviewButtonPlusDials" },
              { 0x002Du, "AssignMovieRecordButtonPlusDials" },
              { 0x002Eu, "FineTuneOptHighlightWeighted" },
              { 0x002Fu, "DynamicAreaAFDisplay" },
              { 0x0030u, "MatrixMetering" },
              { 0x0031u, "LimitAFAreaModeSelection" },
              { 0x0034u, "LensFocusFunctionButtons" },
              { 0x0042u, "VerticalMultiSelector" },
              { 0x0043u, "VerticalFuncButton" },
              { 0x0044u, "VerticalFuncPlusDials" },
              { 0x0046u, "AF-OnButton" },
              { 0x0047u, "SubSelector" },
              { 0x0048u, "SubSelectorCenter" },
              { 0x0049u, "SubSelectorPlusDials" },
              { 0x004Au, "AssignMovieSubselector" },
              { 0x004Bu, "AssignMovieFunc1ButtonPlusDials" },
              { 0x004Cu, "AssignMovieSubselectorPlusDials" },
              { 0x004Du, "SyncReleaseMode" },
              { 0x004Eu, "Three-DTrackingWatchArea" },
              { 0x004Fu, "VerticalAFOnButton" },
              { 0x0050u, "Func2Button" },
              { 0x0051u, "Func2ButtonPlusDials" },
              { 0x0052u, "AssignMovieFunc2Button" },
              { 0x0053u, "Func3Button" },
          };

    static constexpr MakerNoteTagNameEntry
        kMakernoteNikoncustomSettingsd500Extra[]
        = {
              { 0x0000u, "CustomSettingsBank" },
              { 0x0001u, "AF-CPrioritySelection" },
              { 0x0002u, "FocusPointWrap" },
              { 0x0004u, "ISODisplay" },
              { 0x0005u, "LCDIllumination" },
              { 0x0006u, "ReverseIndicators" },
              { 0x0007u, "ExposureControlStepSize" },
              { 0x0008u, "CenterWeightedAreaSize" },
              { 0x0009u, "FineTuneOptCenterWeighted" },
              { 0x000Au, "MultiSelectorShootMode" },
              { 0x000Bu, "ExposureDelayMode" },
              { 0x000Cu, "MaxContinuousRelease" },
              { 0x000Du, "AutoBracketOrder" },
              { 0x000Eu, "Func1Button" },
              { 0x000Fu, "PreviewButton" },
              { 0x0010u, "AssignBktButton" },
              { 0x0012u, "CommandDialsChangeMainSub" },
              { 0x0013u, "StandbyTimer" },
              { 0x0014u, "SelfTimerTime" },
              { 0x0015u, "ImageReviewMonitorOffTime" },
              { 0x0016u, "MenuMonitorOffTime" },
              { 0x0017u, "FlashSyncSpeed" },
              { 0x001Fu, "ModelingFlash" },
              { 0x0024u, "PlaybackMonitorOffTime" },
              { 0x0025u, "MultiSelectorLiveView" },
              { 0x0026u, "ShutterSpeedLock" },
              { 0x0029u, "MovieFunc1Button" },
              { 0x002Au, "Func1ButtonPlusDials" },
              { 0x002Bu, "PreviewButtonPlusDials" },
              { 0x002Du, "AssignMovieRecordButtonPlusDials" },
              { 0x002Eu, "FineTuneOptHighlightWeighted" },
              { 0x002Fu, "DynamicAreaAFDisplay" },
              { 0x0030u, "MatrixMetering" },
              { 0x0031u, "LimitAFAreaModeSelection" },
              { 0x0034u, "LensFocusFunctionButtons" },
              { 0x0042u, "VerticalMultiSelector" },
              { 0x0043u, "AssignMB-D17FuncButton" },
              { 0x0044u, "AssignMB-D17FuncButtonPlusDials" },
              { 0x0046u, "AF-OnButton" },
              { 0x0047u, "SubSelector" },
              { 0x0048u, "SubSelectorCenter" },
              { 0x0049u, "SubSelectorPlusDials" },
              { 0x004Au, "AssignMovieSubselector" },
              { 0x004Bu, "AssignMovieFunc1ButtonPlusDials" },
              { 0x004Cu, "AssignMovieSubselectorPlusDials" },
              { 0x004Du, "SyncReleaseMode" },
              { 0x004Eu, "Three-DTrackingWatchArea" },
              { 0x004Fu, "AssignMB-D17AF-OnButton" },
              { 0x0050u, "Func2Button" },
              { 0x0052u, "AssignMovieFunc2Button" },
          };

    static constexpr MakerNoteTagNameEntry
        kMakernoteNikoncustomSettingsd610Extra[]
        = {
              { 0x0000u, "AF-CPrioritySelection" },
              { 0x0001u, "FocusPointWrap" },
              { 0x0005u, "EasyExposureCompensation" },
              { 0x0006u, "ExposureControlStep" },
              { 0x0007u, "CenterWeightedAreaSize" },
              { 0x0008u, "FineTuneOptCenterWeighted" },
              { 0x0011u, "ShutterReleaseButtonAE-L" },
              { 0x0012u, "StandbyTimer" },
              { 0x0013u, "SelfTimerTime" },
              { 0x0014u, "ImageReviewMonitorOffTime" },
              { 0x0015u, "MenuMonitorOffTime" },
              { 0x0023u, "PlaybackMonitorOffTime" },
          };

    static constexpr MakerNoteTagNameEntry
        kMakernoteNikoncustomSettingsd7000Extra[]
        = {
              { 0x0000u, "AF-CPrioritySelection" },
              { 0x0001u, "FocusPointWrap" },
              { 0x0002u, "BatteryOrder" },
              { 0x0003u, "BeepPitch" },
              { 0x0004u, "ShootingInfoDisplay" },
              { 0x0005u, "ReverseIndicators" },
              { 0x0006u, "ExposureControlStep" },
              { 0x0007u, "CenterWeightedAreaSize" },
              { 0x000Au, "ExposureDelayMode" },
              { 0x000Bu, "MaxContinuousRelease" },
              { 0x000Cu, "AutoBracketSet" },
              { 0x000Du, "FuncButton" },
              { 0x000Eu, "PreviewButton" },
              { 0x000Fu, "OKButton" },
              { 0x0010u, "AELockButton" },
              { 0x0011u, "CommandDialsReverseRotation" },
              { 0x0012u, "MeteringTime" },
              { 0x0013u, "SelfTimerTime" },
              { 0x0014u, "ImageReviewTime" },
              { 0x0015u, "MenuMonitorOffTime" },
              { 0x0016u, "FlashSyncSpeed" },
              { 0x0017u, "FlashControlBuilt-in" },
              { 0x001Eu, "FlashWarning" },
              { 0x0022u, "LiveViewAFAreaMode" },
              { 0x0023u, "PlaybackMonitorOffTime" },
          };

    static constexpr MakerNoteTagNameEntry
        kMakernoteNikoncustomSettingsd800Extra[]
        = {
              { 0x000Cu, "AutoBracketingSet" },
              { 0x0016u, "FlashSyncSpeed" },
              { 0x0017u, "FlashControlBuilt-in" },
              { 0x0019u, "CommanderChannel" },
              { 0x001Bu, "CommanderInternalFlash" },
              { 0x001Cu, "CommanderGroupAMode" },
              { 0x001Du, "CommanderGroupBMode" },
              { 0x001Eu, "ModelingFlash" },
              { 0x001Fu, "CommanderGroupA_TTL-AAComp" },
              { 0x0020u, "CommanderGroupB_TTL-AAComp" },
          };

    static constexpr MakerNoteTagNameEntry
        kMakernoteNikoncustomSettingsd810Extra[]
        = {
              { 0x0000u, "LightSwitch" },
              { 0x0001u, "AF-CPrioritySelection" },
              { 0x0002u, "AFActivation" },
              { 0x0003u, "BatteryOrder" },
              { 0x0004u, "Pitch" },
              { 0x0005u, "ShootingInfoDisplay" },
              { 0x0006u, "ReverseIndicators" },
              { 0x0007u, "ExposureControlStepSize" },
              { 0x0008u, "CenterWeightedAreaSize" },
              { 0x0009u, "FineTuneOptCenterWeighted" },
              { 0x000Au, "MultiSelectorShootMode" },
              { 0x000Bu, "ExposureDelayMode" },
              { 0x000Cu, "MaxContinuousRelease" },
              { 0x000Du, "AutoBracketSet" },
              { 0x000Eu, "FuncButton" },
              { 0x000Fu, "PreviewButton" },
              { 0x0010u, "AssignBktButton" },
              { 0x0011u, "AELockButton" },
              { 0x0012u, "CommandDialsChangeMainSub" },
              { 0x0013u, "StandbyTimer" },
              { 0x0014u, "SelfTimerTime" },
              { 0x0015u, "ImageReviewMonitorOffTime" },
              { 0x0016u, "MenuMonitorOffTime" },
              { 0x0017u, "FlashSyncSpeed" },
              { 0x0018u, "FlashControlBuilt-in" },
              { 0x001Fu, "ModelingFlash" },
              { 0x0024u, "PlaybackMonitorOffTime" },
              { 0x0025u, "MultiSelectorLiveView" },
              { 0x0026u, "ShutterSpeedLock" },
              { 0x0028u, "MovieAELockButtonAssignment" },
              { 0x0029u, "MovieFunctionButton" },
              { 0x002Au, "FuncButtonPlusDials" },
              { 0x002Bu, "PreviewButtonPlusDials" },
              { 0x002Cu, "AELockButtonPlusDials" },
              { 0x002Du, "AssignMovieRecordButton" },
              { 0x002Eu, "FineTuneOptHighlightWeighted" },
              { 0x002Fu, "DynamicAreaAFDisplay" },
              { 0x0030u, "MatrixMetering" },
              { 0x0031u, "LimitAFAreaModeSelection" },
              { 0x0032u, "AF-OnForMB-D12" },
              { 0x0033u, "AssignRemoteFnButton" },
              { 0x0034u, "LensFocusFunctionButtons" },
          };

    static constexpr MakerNoteTagNameEntry
        kMakernoteNikoncustomSettingsd850Extra[]
        = {
              { 0x0000u, "CustomSettingsBank" },
              { 0x0001u, "AF-CPrioritySelection" },
              { 0x0002u, "FocusPointWrap" },
              { 0x0004u, "ISODisplay" },
              { 0x0005u, "LCDIllumination" },
              { 0x0006u, "ReverseIndicators" },
              { 0x0007u, "ExposureControlStepSize" },
              { 0x0008u, "CenterWeightedAreaSize" },
              { 0x0009u, "FineTuneOptCenterWeighted" },
              { 0x000Au, "MultiSelectorShootMode" },
              { 0x000Bu, "ExposureDelayMode" },
              { 0x000Cu, "MaxContinuousRelease" },
              { 0x000Du, "AutoBracketOrder" },
              { 0x000Eu, "Func1Button" },
              { 0x000Fu, "PreviewButton" },
              { 0x0010u, "AssignBktButton" },
              { 0x0012u, "CommandDialsChangeMainSub" },
              { 0x0013u, "StandbyTimer" },
              { 0x0014u, "SelfTimerTime" },
              { 0x0015u, "ImageReviewMonitorOffTime" },
              { 0x0016u, "MenuMonitorOffTime" },
              { 0x0017u, "FlashSyncSpeed" },
              { 0x001Fu, "ModelingFlash" },
              { 0x0024u, "PlaybackMonitorOffTime" },
              { 0x0025u, "MultiSelectorLiveView" },
              { 0x0026u, "ShutterSpeedLock" },
              { 0x0029u, "MovieFunc1Button" },
              { 0x002Au, "Func1ButtonPlusDials" },
              { 0x002Bu, "PreviewButtonPlusDials" },
              { 0x002Du, "AssignMovieRecordButtonPlusDials" },
              { 0x002Eu, "FineTuneOptHighlightWeighted" },
              { 0x002Fu, "DynamicAreaAFDisplay" },
              { 0x0030u, "MatrixMetering" },
              { 0x0031u, "LimitAFAreaModeSelection" },
              { 0x0034u, "LensFocusFunctionButtons" },
              { 0x0042u, "VerticalMultiSelector" },
              { 0x0043u, "AssignMB-D18FuncButton" },
              { 0x0044u, "AssignMB-D18FuncButtonPlusDials" },
              { 0x0046u, "AF-OnButton" },
              { 0x0047u, "SubSelector" },
              { 0x0048u, "SubSelectorCenter" },
              { 0x0049u, "SubSelectorPlusDials" },
              { 0x004Au, "AssignMovieSubselector" },
              { 0x004Bu, "AssignMovieFunc1ButtonPlusDials" },
              { 0x004Cu, "AssignMovieSubselectorPlusDials" },
              { 0x004Du, "SyncReleaseMode" },
              { 0x004Eu, "Three-DTrackingWatchArea" },
              { 0x004Fu, "AssignMB-D18AF-OnButton" },
              { 0x0050u, "Func2Button" },
              { 0x0052u, "AssignMovieFunc2Button" },
          };

    static bool is_ascii_digit(char c) noexcept { return c >= '0' && c <= '9'; }


    struct MkIfdParts final {
        std::string_view vendor;
        std::string_view subtable;
    };

    static MkIfdParts parse_mk_ifd_token(std::string_view ifd) noexcept
    {
        MkIfdParts out;
        if (!ifd.starts_with("mk_")) {
            return out;
        }
        std::string_view rest = ifd.substr(3);
        if (rest.empty()) {
            return out;
        }

        // Strip trailing numeric index suffix (e.g. mk_canon0, mk_casio_type2_0).
        size_t end = rest.size();
        while (end > 0 && is_ascii_digit(rest[end - 1])) {
            end -= 1;
        }
        rest = rest.substr(0, end);

        // Optional '_' delimiter before the index.
        while (!rest.empty() && rest.back() == '_') {
            rest = rest.substr(0, rest.size() - 1);
        }
        if (rest.empty()) {
            return out;
        }

        const size_t sep = rest.find('_');
        if (sep == std::string_view::npos) {
            out.vendor = rest;
            return out;
        }
        if (sep == 0 || sep + 1 >= rest.size()) {
            return out;
        }
        out.vendor   = rest.substr(0, sep);
        out.subtable = rest.substr(sep + 1);
        return out;
    }


    static int compare_key_to_cstr(std::string_view a, const char* b) noexcept
    {
        // Lexicographic compare of a string_view to a NUL-terminated string.
        // Avoids strlen() on every comparison.
        size_t i = 0;
        for (; i < a.size(); ++i) {
            const char bc = b ? b[i] : '\0';
            if (bc == '\0') {
                // b shorter -> a greater.
                return 1;
            }
            const char ac = a[i];
            if (ac < bc) {
                return -1;
            }
            if (ac > bc) {
                return 1;
            }
        }
        // a exhausted.
        const char bc = b ? b[i] : '\0';
        return (bc == '\0') ? 0 : -1;
    }


    static const MakerNoteTableMap* find_table(std::string_view key) noexcept
    {
        const uint32_t count = static_cast<uint32_t>(
            sizeof(kMakerNoteTables) / sizeof(kMakerNoteTables[0]));

        uint32_t lo = 0;
        uint32_t hi = count;
        while (lo < hi) {
            const uint32_t mid         = lo + (hi - lo) / 2U;
            const MakerNoteTableMap& t = kMakerNoteTables[mid];
            if (!t.key) {
                // Generated tables should never include null keys, but don't
                // crash if they do.
                return nullptr;
            }
            const int cmp = compare_key_to_cstr(key, t.key);
            if (cmp == 0) {
                return &t;
            }
            if (cmp < 0) {
                hi = mid;
            } else {
                lo = mid + 1U;
            }
        }
        return nullptr;
    }


    static std::string_view find_tag_name(const MakerNoteTagNameEntry* entries,
                                          uint32_t count, uint16_t tag) noexcept
    {
        if (!entries || count == 0) {
            return {};
        }

        uint32_t lo = 0;
        uint32_t hi = count;
        while (lo < hi) {
            const uint32_t mid = lo + (hi - lo) / 2;
            const uint16_t cur = entries[mid].tag;
            if (cur < tag) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        if (lo < count && entries[lo].tag == tag && entries[lo].name) {
            return entries[lo].name;
        }
        return {};
    }


    static std::string_view
    find_nikoncustom_extra_name(std::string_view subtable,
                                uint16_t tag) noexcept
    {
        if (subtable == "settingsd4") {
            return find_tag_name(
                kMakernoteNikoncustomSettingsd4Extra,
                static_cast<uint32_t>(
                    sizeof(kMakernoteNikoncustomSettingsd4Extra)
                    / sizeof(kMakernoteNikoncustomSettingsd4Extra[0])),
                tag);
        }
        if (subtable == "settingsd5") {
            return find_tag_name(
                kMakernoteNikoncustomSettingsd5Extra,
                static_cast<uint32_t>(
                    sizeof(kMakernoteNikoncustomSettingsd5Extra)
                    / sizeof(kMakernoteNikoncustomSettingsd5Extra[0])),
                tag);
        }
        if (subtable == "settingsd500") {
            return find_tag_name(
                kMakernoteNikoncustomSettingsd500Extra,
                static_cast<uint32_t>(
                    sizeof(kMakernoteNikoncustomSettingsd500Extra)
                    / sizeof(kMakernoteNikoncustomSettingsd500Extra[0])),
                tag);
        }
        if (subtable == "settingsd610") {
            return find_tag_name(
                kMakernoteNikoncustomSettingsd610Extra,
                static_cast<uint32_t>(
                    sizeof(kMakernoteNikoncustomSettingsd610Extra)
                    / sizeof(kMakernoteNikoncustomSettingsd610Extra[0])),
                tag);
        }
        if (subtable == "settingsd7000") {
            return find_tag_name(
                kMakernoteNikoncustomSettingsd7000Extra,
                static_cast<uint32_t>(
                    sizeof(kMakernoteNikoncustomSettingsd7000Extra)
                    / sizeof(kMakernoteNikoncustomSettingsd7000Extra[0])),
                tag);
        }
        if (subtable == "settingsd800") {
            return find_tag_name(
                kMakernoteNikoncustomSettingsd800Extra,
                static_cast<uint32_t>(
                    sizeof(kMakernoteNikoncustomSettingsd800Extra)
                    / sizeof(kMakernoteNikoncustomSettingsd800Extra[0])),
                tag);
        }
        if (subtable == "settingsd810") {
            return find_tag_name(
                kMakernoteNikoncustomSettingsd810Extra,
                static_cast<uint32_t>(
                    sizeof(kMakernoteNikoncustomSettingsd810Extra)
                    / sizeof(kMakernoteNikoncustomSettingsd810Extra[0])),
                tag);
        }
        if (subtable == "settingsd850") {
            return find_tag_name(
                kMakernoteNikoncustomSettingsd850Extra,
                static_cast<uint32_t>(
                    sizeof(kMakernoteNikoncustomSettingsd850Extra)
                    / sizeof(kMakernoteNikoncustomSettingsd850Extra[0])),
                tag);
        }
        return {};
    }


    static const MakerNoteTableMap* try_table(std::string_view vendor_key,
                                              std::string_view table, char* buf,
                                              size_t buf_size) noexcept
    {
        if (!buf || buf_size == 0) {
            return nullptr;
        }

        const std::string_view prefix = "makernote:";
        const std::string_view sep    = ":";

        if (table.empty()) {
            return nullptr;
        }
        if (prefix.size() + vendor_key.size() + sep.size() + table.size()
            >= buf_size) {
            return nullptr;
        }

        size_t n = 0;
        for (size_t i = 0; i < prefix.size(); ++i) {
            buf[n++] = prefix[i];
        }
        for (size_t i = 0; i < vendor_key.size(); ++i) {
            buf[n++] = vendor_key[i];
        }
        for (size_t i = 0; i < sep.size(); ++i) {
            buf[n++] = sep[i];
        }
        for (size_t i = 0; i < table.size(); ++i) {
            buf[n++] = table[i];
        }
        return find_table(std::string_view(buf, n));
    }


    static std::string_view
    synthesize_olympus_placeholder_name(std::string_view subtable,
                                        uint16_t tag) noexcept
    {
        std::string_view prefix;
        if (subtable.empty() || subtable == "main") {
            prefix = "Olympus_0x";
        } else if (subtable == "camerasettings") {
            prefix = "Olympus_CameraSettings_0x";
        } else if (subtable == "fetags") {
            prefix = "Olympus_FETags_0x";
        } else if (subtable == "focusinfo") {
            prefix = "Olympus_FocusInfo_0x";
        } else if (subtable == "imageprocessing") {
            prefix = "Olympus_ImageProcessing_0x";
        } else if (subtable == "rawdevelopment") {
            prefix = "Olympus_RawDevelopment_0x";
        } else if (subtable == "rawdevelopment2") {
            prefix = "Olympus_RawDevelopment2_0x";
        } else if (subtable == "rawinfo") {
            prefix = "Olympus_RawInfo_0x";
        } else if (subtable == "unknowninfo") {
            prefix = "Olympus_UnknownInfo_0x";
        } else {
            return {};
        }

        static thread_local char buf[48];
        static constexpr char kHex[] = "0123456789ABCDEF";
        if (prefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < prefix.size(); ++i) {
            buf[i] = prefix[i];
        }
        buf[prefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[prefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[prefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[prefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[prefix.size() + 4] = '\0';
        return std::string_view(buf, prefix.size() + 4);
    }


    static std::string_view
    synthesize_canon_placeholder_name(std::string_view subtable,
                                      uint16_t tag) noexcept
    {
        if (!(subtable.empty() || subtable == "main")) {
            return {};
        }

        static thread_local char buf[16];
        static constexpr std::string_view kPrefix = "Canon_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[kPrefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[kPrefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[kPrefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[kPrefix.size() + 4] = '\0';
        return std::string_view(buf, kPrefix.size() + 4);
    }


    static std::string_view
    synthesize_flir_placeholder_name(std::string_view subtable,
                                     uint16_t tag) noexcept
    {
        if (!(subtable.empty() || subtable == "main")) {
            return {};
        }

        static thread_local char buf[16];
        static constexpr std::string_view kPrefix = "FLIR_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[kPrefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[kPrefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[kPrefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[kPrefix.size() + 4] = '\0';
        return std::string_view(buf, kPrefix.size() + 4);
    }

    static std::string_view
    synthesize_panasonic_placeholder_name(std::string_view subtable,
                                          uint16_t tag) noexcept
    {
        if (!(subtable.empty() || subtable == "main")) {
            return {};
        }

        static thread_local char buf[21];
        static constexpr std::string_view kPrefix = "Panasonic_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[kPrefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[kPrefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[kPrefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[kPrefix.size() + 4] = '\0';
        return std::string_view(buf, kPrefix.size() + 4);
    }

    static std::string_view
    synthesize_apple_placeholder_name(std::string_view subtable,
                                      uint16_t tag) noexcept
    {
        if (!(subtable.empty() || subtable == "main")) {
            return {};
        }

        static thread_local char buf[17];
        static constexpr std::string_view kPrefix = "Apple_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[kPrefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[kPrefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[kPrefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[kPrefix.size() + 4] = '\0';
        return std::string_view(buf, kPrefix.size() + 4);
    }

    static std::string_view
    synthesize_dji_placeholder_name(std::string_view subtable,
                                    uint16_t tag) noexcept
    {
        if (!(subtable.empty() || subtable == "main")) {
            return {};
        }

        static thread_local char buf[15];
        static constexpr std::string_view kPrefix = "DJI_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[kPrefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[kPrefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[kPrefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[kPrefix.size() + 4] = '\0';
        return std::string_view(buf, kPrefix.size() + 4);
    }

    static std::string_view
    synthesize_fujifilm_placeholder_name(std::string_view subtable,
                                         uint16_t tag) noexcept
    {
        if (!(subtable.empty() || subtable == "main")) {
            return {};
        }

        static thread_local char buf[20];
        static constexpr std::string_view kPrefix = "FujiFilm_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[kPrefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[kPrefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[kPrefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[kPrefix.size() + 4] = '\0';
        return std::string_view(buf, kPrefix.size() + 4);
    }

    static std::string_view
    synthesize_motorola_placeholder_name(std::string_view subtable,
                                         uint16_t tag) noexcept
    {
        if (!(subtable.empty() || subtable == "main")) {
            return {};
        }

        static thread_local char buf[20];
        static constexpr std::string_view kPrefix = "Motorola_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[kPrefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[kPrefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[kPrefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[kPrefix.size() + 4] = '\0';
        return std::string_view(buf, kPrefix.size() + 4);
    }

    static std::string_view
    synthesize_pentax_placeholder_name(std::string_view subtable,
                                       uint16_t tag) noexcept
    {
        if (subtable != "type2" && tag == 0x003EU) {
            return "PreviewImageBorders";
        }

        std::string_view prefix = "Pentax_0x";
        if (subtable == "type2") {
            prefix = "Pentax_Type2_0x";
        }

        static thread_local char buf[24];
        static constexpr char kHex[] = "0123456789abcdef";
        if (prefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < prefix.size(); ++i) {
            buf[i] = prefix[i];
        }
        buf[prefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[prefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[prefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[prefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[prefix.size() + 4] = '\0';
        return std::string_view(buf, prefix.size() + 4);
    }

    static std::string_view
    synthesize_samsung_placeholder_name(std::string_view subtable,
                                        uint16_t tag) noexcept
    {
        std::string_view prefix;
        if (subtable == "ifd") {
            prefix = "Samsung_IFD_0x";
        } else if (subtable == "type2") {
            prefix = "Samsung_Type2_0x";
        } else {
            return {};
        }

        static thread_local char buf[32];
        static constexpr char kHex[] = "0123456789abcdef";
        if (prefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < prefix.size(); ++i) {
            buf[i] = prefix[i];
        }
        buf[prefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[prefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[prefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[prefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[prefix.size() + 4] = '\0';
        return std::string_view(buf, prefix.size() + 4);
    }

    static std::string_view
    synthesize_casio_placeholder_name(std::string_view subtable,
                                      uint16_t tag) noexcept
    {
        if (subtable != "type2") {
            return {};
        }

        static thread_local char buf[24];
        static constexpr std::string_view kPrefix = "Casio_Type2_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[kPrefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[kPrefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[kPrefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[kPrefix.size() + 4] = '\0';
        return std::string_view(buf, kPrefix.size() + 4);
    }

    static std::string_view
    synthesize_minolta_placeholder_name(std::string_view subtable,
                                        uint16_t tag) noexcept
    {
        if (!(subtable.empty() || subtable == "main")) {
            return {};
        }

        static thread_local char buf[18];
        static constexpr std::string_view kPrefix = "Minolta_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[kPrefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[kPrefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[kPrefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[kPrefix.size() + 4] = '\0';
        return std::string_view(buf, kPrefix.size() + 4);
    }

    static std::string_view
    synthesize_ricoh_placeholder_name(std::string_view subtable,
                                      uint16_t tag) noexcept
    {
        std::string_view prefix;
        if (subtable.empty() || subtable == "main") {
            prefix = "Ricoh_0x";
        } else if (subtable == "subdir") {
            prefix = "Ricoh_Subdir_0x";
        } else if (subtable == "imageinfo") {
            prefix = "Ricoh_ImageInfo_0x";
        } else if (subtable == "thetasubdir") {
            prefix = "Ricoh_ThetaSubdir_0x";
        } else if (subtable == "type2") {
            prefix = "Ricoh_Type2_0x";
        } else {
            return {};
        }

        static thread_local char buf[32];
        static constexpr char kHex[] = "0123456789abcdef";
        if (prefix.size() + 4U >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < prefix.size(); ++i) {
            buf[i] = prefix[i];
        }
        buf[prefix.size() + 0U] = kHex[(tag >> 12U) & 0xFU];
        buf[prefix.size() + 1U] = kHex[(tag >> 8U) & 0xFU];
        buf[prefix.size() + 2U] = kHex[(tag >> 4U) & 0xFU];
        buf[prefix.size() + 3U] = kHex[(tag >> 0U) & 0xFU];
        buf[prefix.size() + 4U] = '\0';
        return std::string_view(buf, prefix.size() + 4U);
    }

    static std::string_view
    synthesize_sigma_placeholder_name(std::string_view subtable,
                                      uint16_t tag) noexcept
    {
        if (!(subtable.empty() || subtable == "main")) {
            return {};
        }

        static thread_local char buf[16];
        static constexpr std::string_view kPrefix = "Sigma_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4U >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0U] = kHex[(tag >> 12U) & 0xFU];
        buf[kPrefix.size() + 1U] = kHex[(tag >> 8U) & 0xFU];
        buf[kPrefix.size() + 2U] = kHex[(tag >> 4U) & 0xFU];
        buf[kPrefix.size() + 3U] = kHex[(tag >> 0U) & 0xFU];
        buf[kPrefix.size() + 4U] = '\0';
        return std::string_view(buf, kPrefix.size() + 4U);
    }

    static std::string_view
    synthesize_nikonsettings_placeholder_name(std::string_view subtable,
                                              uint16_t tag) noexcept
    {
        if (!(subtable.empty() || subtable == "main")) {
            return {};
        }

        static thread_local char buf[25];
        static constexpr std::string_view kPrefix = "NikonSettings_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[kPrefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[kPrefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[kPrefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[kPrefix.size() + 4] = '\0';
        return std::string_view(buf, kPrefix.size() + 4);
    }

    static std::string_view
    synthesize_nikon_placeholder_name(std::string_view subtable,
                                      uint16_t tag) noexcept
    {
        if (!(subtable.empty() || subtable == "main")) {
            return {};
        }

        static thread_local char buf[17];
        static constexpr std::string_view kPrefix = "Nikon_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[kPrefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[kPrefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[kPrefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[kPrefix.size() + 4] = '\0';
        return std::string_view(buf, kPrefix.size() + 4);
    }


    static std::string_view
    synthesize_kodak_placeholder_name(std::string_view subtable,
                                      uint16_t tag) noexcept
    {
        std::string_view prefix_digits = subtable;
        std::string_view prefix;

        if (subtable.starts_with("type") && subtable.size() > 4) {
            prefix_digits = subtable.substr(4);
            prefix        = "Kodak_Type";
        } else if (subtable.starts_with("subifd") && subtable.size() > 6) {
            prefix_digits = subtable.substr(6);
            if (subtable == "subifd255") {
                prefix_digits = "0";
                prefix        = "Kodak_SubIFD";
            } else if (subtable == "subifd0" || subtable == "subifd1"
                       || subtable == "subifd2" || subtable == "subifd5"
                       || subtable == "subifd3") {
                prefix = "Kodak_SubIFD";
            } else {
                return {};
            }
        } else if (subtable == "camerainfo") {
            prefix_digits = {};
            prefix        = "Kodak_CameraInfo";
        } else {
            return {};
        }

        for (size_t i = 0; i < prefix_digits.size(); ++i) {
            if (!is_ascii_digit(prefix_digits[i])) {
                return {};
            }
        }

        static thread_local char buf[32];
        static constexpr char kHex[] = "0123456789abcdef";
        const size_t need            = prefix.size() + prefix_digits.size()
                            + sizeof("_0x0000") - 1U;
        if (need >= sizeof(buf)) {
            return {};
        }

        size_t n = 0;
        for (size_t i = 0; i < prefix.size(); ++i) {
            buf[n++] = prefix[i];
        }
        for (size_t i = 0; i < prefix_digits.size(); ++i) {
            buf[n++] = prefix_digits[i];
        }
        buf[n++] = '_';
        buf[n++] = '0';
        buf[n++] = 'x';
        buf[n++] = kHex[(tag >> 12) & 0xF];
        buf[n++] = kHex[(tag >> 8) & 0xF];
        buf[n++] = kHex[(tag >> 4) & 0xF];
        buf[n++] = kHex[(tag >> 0) & 0xF];
        buf[n]   = '\0';
        return std::string_view(buf, n);
    }


    static std::string_view
    synthesize_canoncustom_functions2_placeholder_name(uint16_t tag) noexcept
    {
        static thread_local char buf[32];
        static constexpr std::string_view kPrefix = "CanonCustom_Functions2_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[kPrefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[kPrefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[kPrefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[kPrefix.size() + 4] = '\0';
        return std::string_view(buf, kPrefix.size() + 4);
    }


    static std::string_view
    synthesize_canoncustom_functionsd30_placeholder_name(uint16_t tag) noexcept
    {
        static thread_local char buf[35];
        static constexpr std::string_view kPrefix
            = "CanonCustom_FunctionsD30_0x";
        static constexpr char kHex[] = "0123456789abcdef";
        if (kPrefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[kPrefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[kPrefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[kPrefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[kPrefix.size() + 4] = '\0';
        return std::string_view(buf, kPrefix.size() + 4);
    }

    static std::string_view
    synthesize_canoncustom_functions5d_placeholder_name(uint16_t tag) noexcept
    {
        static thread_local char buf[34];
        static constexpr std::string_view kPrefix = "CanonCustom_Functions5D_0x";
        static constexpr char kHex[] = "0123456789abcdef";
        if (kPrefix.size() + 4 >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0] = kHex[(tag >> 12) & 0xF];
        buf[kPrefix.size() + 1] = kHex[(tag >> 8) & 0xF];
        buf[kPrefix.size() + 2] = kHex[(tag >> 4) & 0xF];
        buf[kPrefix.size() + 3] = kHex[(tag >> 0) & 0xF];
        buf[kPrefix.size() + 4] = '\0';
        return std::string_view(buf, kPrefix.size() + 4);
    }


    static bool
    olympus_subtable_prefers_placeholder(std::string_view subtable) noexcept
    {
        return subtable == "camerasettings" || subtable == "fetags"
               || subtable == "focusinfo" || subtable == "imageprocessing"
               || subtable == "rawdevelopment" || subtable == "rawdevelopment2"
               || subtable == "rawinfo" || subtable == "unknowninfo";
    }


    static bool olympus_main_prefers_placeholder(uint16_t tag) noexcept
    {
        return tag == 0x0400u || tag == 0x0401u;
    }

    static bool apple_main_prefers_placeholder(uint16_t tag) noexcept
    {
        return tag == 0x001Fu || tag == 0x0023u || tag == 0x002Du
               || tag == 0x002Eu;
    }


    static bool olympus_focusinfo_prefers_placeholder(uint16_t tag) noexcept
    {
        return tag == 0x2100u;
    }


    static bool olympus_fetags_prefers_cross_table_name(uint16_t tag) noexcept
    {
        return tag == 0x0311u || tag == 0x1204u;
    }


    static bool olympus_camerasettings_prefers_placeholder(uint16_t tag) noexcept
    {
        return tag == 0x030Au || tag == 0x030Bu || tag == 0x0821u;
    }


    static bool
    olympus_imageprocessing_prefers_placeholder(uint16_t tag) noexcept
    {
        return tag == 0x2110u;
    }

    static std::string_view
    find_unique_tag_name_by_key_prefix(std::string_view prefix,
                                       uint16_t tag) noexcept
    {
        const uint32_t count = static_cast<uint32_t>(
            sizeof(kMakerNoteTables) / sizeof(kMakerNoteTables[0]));
        std::string_view chosen;
        for (uint32_t i = 0; i < count; ++i) {
            const MakerNoteTableMap& table = kMakerNoteTables[i];
            if (!table.key) {
                continue;
            }
            const std::string_view key(table.key);
            if (key.size() < prefix.size()
                || key.substr(0, prefix.size()) != prefix) {
                continue;
            }
            const std::string_view name = find_tag_name(table.entries,
                                                        table.count, tag);
            if (name.empty()) {
                continue;
            }
            if (chosen.empty()) {
                chosen = name;
                continue;
            }
            if (name != chosen) {
                return {};
            }
        }
        return chosen;
    }


    static std::string_view
    find_name_in_candidate_tables(std::string_view vendor_key, uint16_t tag,
                                  std::span<const std::string_view> candidates,
                                  char* table_key_buf,
                                  size_t table_key_buf_size) noexcept
    {
        for (size_t i = 0; i < candidates.size(); ++i) {
            const MakerNoteTableMap* candidate
                = try_table(vendor_key, candidates[i], table_key_buf,
                            table_key_buf_size);
            if (!candidate) {
                continue;
            }
            const std::string_view name = find_tag_name(candidate->entries,
                                                        candidate->count, tag);
            if (!name.empty()) {
                return name;
            }
        }
        return {};
    }


    static bool canon_subtable_family(std::string_view subtable,
                                      std::string_view family) noexcept
    {
        return !subtable.empty() && subtable.starts_with(family);
    }

}  // namespace

std::string_view
makernote_tag_name(std::string_view ifd, uint16_t tag) noexcept
{
    const MkIfdParts parts = parse_mk_ifd_token(ifd);
    if (parts.vendor.empty()) {
        return {};
    }

    // Current MakerNote decode tokens use a few short aliases. Convert them to
    // canonical table keys used by the registry.
    std::string_view vendor_key = parts.vendor;
    if (vendor_key == "fuji") {
        vendor_key = "fujifilm";
    }

    if (vendor_key == "nikoncustom" && !parts.subtable.empty()) {
        const std::string_view extra_name
            = find_nikoncustom_extra_name(parts.subtable, tag);
        if (!extra_name.empty()) {
            return extra_name;
        }
    }

    const bool canon_colordata_generic = vendor_key == "canon"
                                         && parts.subtable == "colordata";
    const bool canon_colordata_specific
        = vendor_key == "canon"
          && canon_subtable_family(parts.subtable, "colordata")
          && parts.subtable != "colordata";
    const bool canon_camerainfo_generic = vendor_key == "canon"
                                          && parts.subtable == "camerainfo";
    const bool canon_camerainfo_specific
        = vendor_key == "canon"
          && canon_subtable_family(parts.subtable, "camerainfo")
          && parts.subtable != "camerainfo";
    const bool canon_psinfo_family = vendor_key == "canon"
                                     && canon_subtable_family(parts.subtable,
                                                              "psinfo");

    char table_key_buf[96];

    const MakerNoteTableMap* table = nullptr;
    if (!parts.subtable.empty()) {
        table = try_table(vendor_key, parts.subtable, table_key_buf,
                          sizeof(table_key_buf));
    }
    if (!table) {
        table = try_table(vendor_key, "main", table_key_buf,
                          sizeof(table_key_buf));
    }
    if (!table) {
        if (vendor_key == "kodak" && !parts.subtable.empty()) {
            return synthesize_kodak_placeholder_name(parts.subtable, tag);
        }
        if (vendor_key == "sigma"
            && (parts.subtable.empty() || parts.subtable == "main")) {
            return synthesize_sigma_placeholder_name(parts.subtable, tag);
        }
        return {};
    }
    std::string_view name = find_tag_name(table->entries, table->count, tag);
    if (!name.empty()) {
        if (vendor_key == "sigma"
            && (parts.subtable.empty() || parts.subtable == "main")
            && tag == 0x000CU) {
            return "ExposureAdjust";
        }
        if (vendor_key == "minolta"
            && (parts.subtable.empty() || parts.subtable == "main")
            && tag == 0x0103U) {
            return "MinoltaImageSize";
        }
        if (vendor_key == "samsung" && parts.subtable == "ifd") {
            return synthesize_samsung_placeholder_name(parts.subtable, tag);
        }
        if (vendor_key == "olympus"
            && (parts.subtable.empty() || parts.subtable == "main")
            && olympus_main_prefers_placeholder(tag)) {
            return synthesize_olympus_placeholder_name(parts.subtable, tag);
        }
        if (vendor_key == "apple"
            && (parts.subtable.empty() || parts.subtable == "main")
            && apple_main_prefers_placeholder(tag)) {
            return synthesize_apple_placeholder_name(parts.subtable, tag);
        }
        if (vendor_key == "olympus" && parts.subtable == "unknowninfo") {
            return synthesize_olympus_placeholder_name(parts.subtable, tag);
        }
        if (vendor_key == "olympus" && parts.subtable == "focusinfo"
            && olympus_focusinfo_prefers_placeholder(tag)) {
            return synthesize_olympus_placeholder_name(parts.subtable, tag);
        }
        if (vendor_key == "olympus" && parts.subtable == "camerasettings"
            && olympus_camerasettings_prefers_placeholder(tag)) {
            return synthesize_olympus_placeholder_name(parts.subtable, tag);
        }
        if (vendor_key == "olympus" && parts.subtable == "imageprocessing"
            && olympus_imageprocessing_prefers_placeholder(tag)) {
            return synthesize_olympus_placeholder_name(parts.subtable, tag);
        }
        return name;
    }

    // Canon uses many model/version-specific table names for common decoded
    // subtables (`camerainfo*`, `colordata*`) while decode emits stable token
    // names (`mk_canon_camerainfo_*`, `mk_canon_colordata_*`).
    if (canon_colordata_generic) {
        name = find_unique_tag_name_by_key_prefix("makernote:canon:colordata",
                                                  tag);
        if (!name.empty()) {
            return name;
        }
        return {};
    }
    if (canon_colordata_specific) {
        return {};
    }
    if (canon_camerainfo_generic) {
        name = find_unique_tag_name_by_key_prefix("makernote:canon:camerainfo",
                                                  tag);
        if (!name.empty()) {
            return name;
        }
        return {};
    }
    if (canon_camerainfo_specific) {
        return {};
    }
    if (canon_psinfo_family) {
        return {};
    }

    if (vendor_key == "panasonic" && parts.subtable.empty()) {
        // Older Panasonic maker notes often align with the legacy `pana` table.
        static constexpr std::string_view kPanasonicMainFallbacks[] = {
            "pana",
        };
        name = find_name_in_candidate_tables(vendor_key, tag,
                                             std::span<const std::string_view>(
                                                 kPanasonicMainFallbacks),
                                             table_key_buf,
                                             sizeof(table_key_buf));
        if (!name.empty()) {
            return name;
        }
    }

    if (vendor_key == "kodak" && parts.subtable.empty()) {
        // Kodak type-specific tables share tag ids; only use unambiguous
        // matches across the family to avoid introducing wrong names.
        name = find_unique_tag_name_by_key_prefix("makernote:kodak:type", tag);
        if (!name.empty()) {
            return name;
        }
        name = find_unique_tag_name_by_key_prefix("makernote:kodak:subifd",
                                                  tag);
        if (!name.empty()) {
            return name;
        }
        static constexpr std::string_view kKodakMainFallbacks[] = {
            "ifd",
            "camerainfo",
        };
        name = find_name_in_candidate_tables(vendor_key, tag,
                                             std::span<const std::string_view>(
                                                 kKodakMainFallbacks),
                                             table_key_buf,
                                             sizeof(table_key_buf));
        if (!name.empty()) {
            return name;
        }
    }

    if (vendor_key == "olympus" && parts.subtable == "fetags"
        && olympus_fetags_prefers_cross_table_name(tag)) {
        static constexpr std::string_view kOlympusFeFallbacks[] = {
            "camerasettings", "focusinfo", "imageprocessing",
            "rawinfo",        "equipment", "main",
        };
        name = find_name_in_candidate_tables(vendor_key, tag,
                                             std::span<const std::string_view>(
                                                 kOlympusFeFallbacks),
                                             table_key_buf,
                                             sizeof(table_key_buf));
        if (!name.empty()) {
            return name;
        }
    }

    if (vendor_key == "olympus"
        && olympus_subtable_prefers_placeholder(parts.subtable)) {
        return synthesize_olympus_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "apple"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_apple_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "panasonic"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_panasonic_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "fujifilm"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_fujifilm_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "dji"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_dji_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "sigma"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_sigma_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "samsung"
        && (parts.subtable == "ifd" || parts.subtable == "type2")) {
        return synthesize_samsung_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "casio" && parts.subtable == "type2") {
        return synthesize_casio_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "minolta"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_minolta_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "motorola"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_motorola_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "pentax") {
        return synthesize_pentax_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "ricoh"
        && (parts.subtable.empty() || parts.subtable == "main"
            || parts.subtable == "subdir" || parts.subtable == "imageinfo"
            || parts.subtable == "thetasubdir" || parts.subtable == "type2")) {
        return synthesize_ricoh_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "nikonsettings"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_nikonsettings_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "nikon"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_nikon_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "kodak" && !parts.subtable.empty()) {
        return synthesize_kodak_placeholder_name(parts.subtable, tag);
    }

    if (vendor_key == "canon"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_canon_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "flir"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_flir_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "canon" && !parts.subtable.empty()) {
        return {};
    }
    if (vendor_key == "canoncustom" && parts.subtable == "functions2") {
        return synthesize_canoncustom_functions2_placeholder_name(tag);
    }
    if (vendor_key == "canoncustom" && parts.subtable == "functionsd30") {
        return synthesize_canoncustom_functionsd30_placeholder_name(tag);
    }
    if (vendor_key == "canoncustom" && parts.subtable == "functions5d") {
        return synthesize_canoncustom_functions5d_placeholder_name(tag);
    }

    // Some MakerNote subtables omit tags that still exist in the vendor's
    // main table. Fallback improves practical name coverage without changing
    // decode semantics.
    const MakerNoteTableMap* main_table
        = try_table(vendor_key, "main", table_key_buf, sizeof(table_key_buf));
    if (!main_table || main_table == table) {
        if (vendor_key == "olympus") {
            return synthesize_olympus_placeholder_name(parts.subtable, tag);
        }
        return {};
    }
    name = find_tag_name(main_table->entries, main_table->count, tag);
    if (!name.empty()) {
        if (vendor_key == "apple"
            && (parts.subtable.empty() || parts.subtable == "main")
            && apple_main_prefers_placeholder(tag)) {
            return synthesize_apple_placeholder_name(parts.subtable, tag);
        }
        return name;
    }
    if (vendor_key == "olympus") {
        return synthesize_olympus_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "apple"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_apple_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "panasonic"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_panasonic_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "fujifilm"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_fujifilm_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "dji"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_dji_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "sigma"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_sigma_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "casio" && parts.subtable == "type2") {
        return synthesize_casio_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "minolta"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_minolta_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "motorola"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_motorola_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "pentax") {
        return synthesize_pentax_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "ricoh"
        && (parts.subtable.empty() || parts.subtable == "main"
            || parts.subtable == "subdir" || parts.subtable == "imageinfo"
            || parts.subtable == "thetasubdir" || parts.subtable == "type2")) {
        return synthesize_ricoh_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "nikonsettings"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_nikonsettings_placeholder_name(parts.subtable, tag);
    }
    if (vendor_key == "nikon"
        && (parts.subtable.empty() || parts.subtable == "main")) {
        return synthesize_nikon_placeholder_name(parts.subtable, tag);
    }
    return {};
}

}  // namespace openmeta
