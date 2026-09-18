#include <custommodel/pe_image.hpp>

#include <cstring>

namespace custommodel {
namespace {

constexpr std::size_t kDosSignatureOffset = 0x00;
constexpr std::size_t kNtHeaderOffsetField = 0x3C;
constexpr std::size_t kDosHeaderMinimumSize = 0x40;
constexpr std::size_t kCoffHeaderSize = 20;
constexpr std::size_t kOptionalHeaderMagicOffset = 0;
constexpr std::size_t kEntryPointOffset = 16;
constexpr std::size_t kImageSizeOffset = 56;
constexpr std::size_t kRequiredOptionalHeaderSize = 60;
constexpr std::uint16_t kDosSignature = 0x5A4D;
constexpr std::uint32_t kNtSignature = 0x00004550;
constexpr std::uint16_t kPe32Magic = 0x010B;
constexpr std::uint16_t kPe32PlusMagic = 0x020B;

bool CanRead(std::size_t offset, std::size_t length, std::size_t size) noexcept {
    return offset <= size && length <= size - offset;
}

template <typename T>
bool Read(const std::uint8_t* data, std::size_t size, std::size_t offset, T& value) noexcept {
    if (!CanRead(offset, sizeof(T), size)) {
        return false;
    }

    std::memcpy(&value, data + offset, sizeof(T));
    return true;
}

}

PeParseResult ParsePeImage(const std::uint8_t* data, std::size_t size) noexcept {
    if (data == nullptr) {
        return {{}, PeParseError::NullInput};
    }

    if (size < kDosHeaderMinimumSize) {
        return {{}, PeParseError::TruncatedDosHeader};
    }

    std::uint16_t dosSignature{};
    Read(data, size, kDosSignatureOffset, dosSignature);
    if (dosSignature != kDosSignature) {
        return {{}, PeParseError::InvalidDosSignature};
    }

    std::uint32_t ntHeaderOffset{};
    Read(data, size, kNtHeaderOffsetField, ntHeaderOffset);
    const auto ntOffset = static_cast<std::size_t>(ntHeaderOffset);
    if (!CanRead(ntOffset, sizeof(std::uint32_t) + kCoffHeaderSize, size)) {
        return {{}, PeParseError::TruncatedNtHeaders};
    }

    std::uint32_t ntSignature{};
    Read(data, size, ntOffset, ntSignature);
    if (ntSignature != kNtSignature) {
        return {{}, PeParseError::InvalidNtSignature};
    }

    const auto coffOffset = ntOffset + sizeof(std::uint32_t);
    std::uint16_t optionalHeaderSize{};
    Read(data, size, coffOffset + 16, optionalHeaderSize);
    if (optionalHeaderSize < kRequiredOptionalHeaderSize) {
        return {{}, PeParseError::TruncatedOptionalHeader};
    }

    const auto optionalOffset = coffOffset + kCoffHeaderSize;
    if (!CanRead(optionalOffset, optionalHeaderSize, size)) {
        return {{}, PeParseError::TruncatedOptionalHeader};
    }

    std::uint16_t optionalMagic{};
    Read(data, size, optionalOffset + kOptionalHeaderMagicOffset, optionalMagic);

    PeImageKind kind{};
    if (optionalMagic == kPe32Magic) {
        kind = PeImageKind::Pe32;
    } else if (optionalMagic == kPe32PlusMagic) {
        kind = PeImageKind::Pe32Plus;
    } else {
        return {{}, PeParseError::UnsupportedOptionalHeader};
    }

    PeImageInfo image{};
    image.kind = kind;
    Read(data, size, optionalOffset + kEntryPointOffset, image.entryPointRva);
    Read(data, size, optionalOffset + kImageSizeOffset, image.sizeOfImage);
    if (image.sizeOfImage == 0) {
        return {{}, PeParseError::InvalidImageSize};
    }

    return {image, PeParseError::None};
}

const char* ToString(PeParseError error) noexcept {
    switch (error) {
    case PeParseError::None:
        return "no error";
    case PeParseError::NullInput:
        return "null input";
    case PeParseError::TruncatedDosHeader:
        return "truncated DOS header";
    case PeParseError::InvalidDosSignature:
        return "invalid DOS signature";
    case PeParseError::TruncatedNtHeaders:
        return "truncated NT headers";
    case PeParseError::InvalidNtSignature:
        return "invalid NT signature";
    case PeParseError::TruncatedOptionalHeader:
        return "truncated optional header";
    case PeParseError::UnsupportedOptionalHeader:
        return "unsupported optional header";
    case PeParseError::InvalidImageSize:
        return "invalid image size";
    }

    return "unknown PE parse error";
}

}
