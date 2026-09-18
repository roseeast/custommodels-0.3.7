#pragma once

#include <array>
#include <cstdint>

namespace custommodel {

struct RuntimeVersion {
    std::uint16_t major{};
    std::uint16_t minor{};
    std::uint16_t patch{};
};

inline constexpr RuntimeVersion kRuntimeVersion{0, 2, 0};

enum class SampVersion : std::uint8_t {
    Unknown,
    R1,
    R2,
    R3,
    R3_1,
    R4,
    R4_2,
    R4Family,
};

struct SampBuildInfo {
    SampVersion version{SampVersion::Unknown};
    std::uint32_t entryPoint{};
    const char* name{"Unknown"};
};

const std::array<SampBuildInfo, 5>& KnownSampBuilds() noexcept;
const SampBuildInfo* FindSampBuild(std::uint32_t entryPoint) noexcept;
const SampBuildInfo* FindSampBuild(SampVersion version) noexcept;
const char* ToString(SampVersion version) noexcept;

}
