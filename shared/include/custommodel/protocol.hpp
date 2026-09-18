#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <custommodel/assets.hpp>
#include <custommodel/version.hpp>

namespace custommodel::protocol {

inline constexpr std::uint32_t kMagic = 0x434D4F44;
inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::size_t kHeaderSize = 12;
inline constexpr std::size_t kMaximumPacketSize = 4096;
inline constexpr std::size_t kMaximumPayloadSize = kMaximumPacketSize - kHeaderSize;

struct ByteView {
    const std::uint8_t* data{};
    std::size_t size{};
};

enum class MessageType : std::uint16_t {
    Hello = 1,
    Welcome = 2,
    Reject = 3,
    Ping = 4,
    Pong = 5,
    ManifestBegin = 6,
    ManifestAsset = 7,
    ManifestEnd = 8,
    AssetReady = 9,
    AssetError = 10,
};

enum class Capability : std::uint32_t {
    CharModel = 1U << 0U,
    ObjectModel = 1U << 1U,
    Dff = 1U << 2U,
    Txd = 1U << 3U,
    Sha256Cache = 1U << 4U,
};

using CapabilityFlags = std::uint32_t;

inline constexpr CapabilityFlags kDeclaredCapabilities =
    static_cast<CapabilityFlags>(Capability::CharModel) |
    static_cast<CapabilityFlags>(Capability::ObjectModel) |
    static_cast<CapabilityFlags>(Capability::Dff) |
    static_cast<CapabilityFlags>(Capability::Txd) |
    static_cast<CapabilityFlags>(Capability::Sha256Cache);

enum class RejectReason : std::uint16_t {
    UnsupportedProtocol = 1,
    UnsupportedClientRuntime = 2,
    UnsupportedSampBuild = 3,
    MalformedHandshake = 4,
    UnsupportedCapabilities = 5,
};

struct Hello {
    RuntimeVersion runtimeVersion{};
    std::uint16_t protocolVersion{};
    SampVersion sampVersion{SampVersion::Unknown};
    CapabilityFlags capabilities{};
};

struct Welcome {
    std::uint16_t acceptedProtocolVersion{};
    RuntimeVersion serverRuntimeVersion{};
    std::uint16_t serverProtocolVersion{};
    CapabilityFlags negotiatedCapabilities{};
};

struct Reject {
    RejectReason reason{RejectReason::MalformedHandshake};
};

struct Ping {
    std::uint64_t nonce{};
};

struct Pong {
    std::uint64_t nonce{};
};

struct ManifestBegin {
    std::uint64_t manifestRevision{};
    std::uint32_t assetCount{};
    std::uint64_t totalDownloadSize{};
};

struct ManifestAsset {
    std::uint64_t manifestRevision{};
    std::uint32_t assetIndex{};
    assets::AssetDescription asset;
};

struct ManifestEnd {
    std::uint64_t manifestRevision{};
    std::uint32_t assetCount{};
    std::uint64_t totalDownloadSize{};
};

enum class AssetErrorReason : std::uint16_t {
    InvalidManifest = 1,
    UnsupportedScheme = 2,
    NetworkFailure = 3,
    Timeout = 4,
    SizeMismatch = 5,
    HashMismatch = 6,
    DiskFailure = 7,
    InternalError = 8,
};

struct AssetReady {
    std::uint64_t manifestRevision{};
    std::uint32_t assetId{};
};

struct AssetError {
    std::uint64_t manifestRevision{};
    std::uint32_t assetId{};
    AssetErrorReason reason{AssetErrorReason::InternalError};
};

using Message = std::variant<
    Hello,
    Welcome,
    Reject,
    Ping,
    Pong,
    ManifestBegin,
    ManifestAsset,
    ManifestEnd,
    AssetReady,
    AssetError
>;

struct Packet {
    std::uint16_t protocolVersion{};
    Message message{};
};

enum class EncodeError : std::uint8_t {
    None,
    InvalidField,
    OversizedPayload,
};

struct EncodeResult {
    std::vector<std::uint8_t> bytes;
    EncodeError error{EncodeError::None};

    explicit operator bool() const noexcept {
        return error == EncodeError::None;
    }
};

enum class DecodeError : std::uint8_t {
    None,
    TruncatedHeader,
    InvalidMagic,
    UnsupportedProtocolVersion,
    OversizedPayload,
    TruncatedPayload,
    PayloadLengthMismatch,
    UnknownMessageType,
    InvalidField,
};

struct DecodeResult {
    std::optional<Packet> packet;
    DecodeError error{DecodeError::None};

    explicit operator bool() const noexcept {
        return error == DecodeError::None;
    }
};

EncodeResult EncodePacket(
    const Message& message,
    std::uint16_t protocolVersion = kProtocolVersion
);
DecodeResult DecodePacket(ByteView bytes) noexcept;

inline DecodeResult DecodePacket(const std::vector<std::uint8_t>& bytes) noexcept {
    return DecodePacket({bytes.data(), bytes.size()});
}

bool HasCapability(CapabilityFlags flags, Capability capability) noexcept;
bool AreCapabilitiesValid(CapabilityFlags flags) noexcept;
const char* ToString(DecodeError error) noexcept;
const char* ToString(RejectReason reason) noexcept;
const char* ToString(AssetErrorReason reason) noexcept;

}
