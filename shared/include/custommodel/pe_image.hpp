#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace custommodel {

enum class PeImageKind : std::uint8_t {
    Pe32,
    Pe32Plus,
};

struct PeImageInfo {
    PeImageKind kind{};
    std::uint32_t entryPointRva{};
    std::uint32_t sizeOfImage{};
};

enum class PeParseError : std::uint8_t {
    None,
    NullInput,
    TruncatedDosHeader,
    InvalidDosSignature,
    TruncatedNtHeaders,
    InvalidNtSignature,
    TruncatedOptionalHeader,
    UnsupportedOptionalHeader,
    InvalidImageSize,
};

struct PeParseResult {
    std::optional<PeImageInfo> image;
    PeParseError error{PeParseError::None};

    explicit operator bool() const noexcept {
        return image.has_value();
    }
};

PeParseResult ParsePeImage(const std::uint8_t* data, std::size_t size) noexcept;
const char* ToString(PeParseError error) noexcept;

}
