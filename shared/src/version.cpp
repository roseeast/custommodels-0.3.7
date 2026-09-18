#include <custommodel/version.hpp>

namespace custommodel {
namespace {

constexpr std::array<SampBuildInfo, 5> kKnownBuilds{{
    {SampVersion::R1, 0x31DF13, "SA-MP 0.3.7 R1"},
    {SampVersion::R2, 0x3195DD, "SA-MP 0.3.7 R2"},
    {SampVersion::R3_1, 0x0CC4D0, "SA-MP 0.3.7 R3-1"},
    {SampVersion::R4, 0x0CBCB0, "SA-MP 0.3.7 R4"},
    {SampVersion::R4Family, 0x0CBCD0, "SA-MP 0.3.7 R4-family"},
}};

}

const std::array<SampBuildInfo, 5>& KnownSampBuilds() noexcept {
    return kKnownBuilds;
}

const SampBuildInfo* FindSampBuild(std::uint32_t entryPoint) noexcept {
    for (const auto& build : kKnownBuilds) {
        if (build.entryPoint == entryPoint) {
            return &build;
        }
    }

    return nullptr;
}

const SampBuildInfo* FindSampBuild(SampVersion version) noexcept {
    for (const auto& build : kKnownBuilds) {
        if (build.version == version) {
            return &build;
        }
    }

    return nullptr;
}

const char* ToString(SampVersion version) noexcept {
    switch (version) {
    case SampVersion::Unknown:
        return "Unknown";
    case SampVersion::R1:
        return "SA-MP 0.3.7 R1";
    case SampVersion::R2:
        return "SA-MP 0.3.7 R2";
    case SampVersion::R3:
        return "SA-MP 0.3.7 R3";
    case SampVersion::R3_1:
        return "SA-MP 0.3.7 R3-1";
    case SampVersion::R4:
        return "SA-MP 0.3.7 R4";
    case SampVersion::R4_2:
        return "SA-MP 0.3.7 R4-2";
    case SampVersion::R4Family:
        return "SA-MP 0.3.7 R4-family";
    }

    return "Unknown";
}

}
