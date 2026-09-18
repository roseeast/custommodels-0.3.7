#include <custommodel/protocol.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace custommodel::protocol {
namespace {

class ByteWriter final {
public:
    bool WriteU8(std::uint8_t value) {
        return WriteByte(value);
    }

    bool WriteU16(std::uint16_t value) {
        return WriteByte(static_cast<std::uint8_t>(value >> 8U)) &&
            WriteByte(static_cast<std::uint8_t>(value));
    }

    bool WriteU32(std::uint32_t value) {
        return WriteByte(static_cast<std::uint8_t>(value >> 24U)) &&
            WriteByte(static_cast<std::uint8_t>(value >> 16U)) &&
            WriteByte(static_cast<std::uint8_t>(value >> 8U)) &&
            WriteByte(static_cast<std::uint8_t>(value));
    }

    bool WriteU64(std::uint64_t value) {
        return WriteU32(static_cast<std::uint32_t>(value >> 32U)) &&
            WriteU32(static_cast<std::uint32_t>(value));
    }

    bool WriteBytes(const std::uint8_t* data, std::size_t size) {
        if (data == nullptr || size > kMaximumPacketSize - bytes_.size()) {
            return false;
        }
        bytes_.insert(bytes_.end(), data, data + size);
        return true;
    }

    bool WriteString(const std::string& value) {
        return value.size() <= std::numeric_limits<std::uint16_t>::max() &&
            WriteU16(static_cast<std::uint16_t>(value.size())) &&
            WriteBytes(
                reinterpret_cast<const std::uint8_t*>(value.data()),
                value.size()
            );
    }

    std::vector<std::uint8_t> Take() {
        return std::move(bytes_);
    }

private:
    bool WriteByte(std::uint8_t value) {
        if (bytes_.size() == kMaximumPacketSize) {
            return false;
        }
        bytes_.push_back(value);
        return true;
    }

    std::vector<std::uint8_t> bytes_;
};

class ByteReader final {
public:
    explicit ByteReader(ByteView bytes) noexcept : bytes_(bytes) {}

    bool ReadU8(std::uint8_t& value) noexcept {
        return ReadByte(value);
    }

    bool ReadU16(std::uint16_t& value) noexcept {
        std::uint8_t high{};
        std::uint8_t low{};
        if (!ReadByte(high) || !ReadByte(low)) {
            return false;
        }
        value = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(high) << 8U) |
            static_cast<std::uint16_t>(low)
        );
        return true;
    }

    bool ReadU32(std::uint32_t& value) noexcept {
        std::uint8_t bytes[4]{};
        for (auto& byte : bytes) {
            if (!ReadByte(byte)) {
                return false;
            }
        }
        value = (static_cast<std::uint32_t>(bytes[0]) << 24U) |
            (static_cast<std::uint32_t>(bytes[1]) << 16U) |
            (static_cast<std::uint32_t>(bytes[2]) << 8U) |
            static_cast<std::uint32_t>(bytes[3]);
        return true;
    }

    bool ReadU64(std::uint64_t& value) noexcept {
        std::uint32_t high{};
        std::uint32_t low{};
        if (!ReadU32(high) || !ReadU32(low)) {
            return false;
        }
        value = (static_cast<std::uint64_t>(high) << 32U) |
            static_cast<std::uint64_t>(low);
        return true;
    }

    bool ReadBytes(std::uint8_t* output, std::size_t size) noexcept {
        if (output == nullptr || size > Remaining() || bytes_.data == nullptr) {
            return false;
        }
        std::copy_n(bytes_.data + offset_, size, output);
        offset_ += size;
        return true;
    }

    bool ReadString(std::string& output, std::size_t maximumLength) {
        std::uint16_t length{};
        if (!ReadU16(length) || length > maximumLength || length > Remaining() ||
            bytes_.data == nullptr) {
            return false;
        }
        output.assign(
            reinterpret_cast<const char*>(bytes_.data + offset_),
            length
        );
        offset_ += length;
        return true;
    }

    std::size_t Remaining() const noexcept {
        return bytes_.size - offset_;
    }

private:
    bool ReadByte(std::uint8_t& value) noexcept {
        if (offset_ >= bytes_.size || bytes_.data == nullptr) {
            return false;
        }
        value = bytes_.data[offset_++];
        return true;
    }

    ByteView bytes_;
    std::size_t offset_{};
};

bool WriteRuntimeVersion(ByteWriter& writer, const RuntimeVersion& version) {
    return writer.WriteU16(version.major) &&
        writer.WriteU16(version.minor) &&
        writer.WriteU16(version.patch);
}

bool ReadRuntimeVersion(ByteReader& reader, RuntimeVersion& version) noexcept {
    return reader.ReadU16(version.major) &&
        reader.ReadU16(version.minor) &&
        reader.ReadU16(version.patch);
}

bool IsSampVersionValueValid(std::uint16_t value) noexcept {
    return value <= static_cast<std::uint16_t>(SampVersion::R4Family);
}

bool IsRejectReasonValid(std::uint16_t value) noexcept {
    return value >= static_cast<std::uint16_t>(RejectReason::UnsupportedProtocol) &&
        value <= static_cast<std::uint16_t>(RejectReason::UnsupportedCapabilities);
}

bool IsAssetErrorReasonValid(std::uint16_t value) noexcept {
    return value >= static_cast<std::uint16_t>(AssetErrorReason::InvalidManifest) &&
        value <= static_cast<std::uint16_t>(AssetErrorReason::InternalError);
}

bool IsManifestBoundaryValid(
    std::uint64_t revision,
    std::uint32_t count,
    std::uint64_t totalDownloadSize
) noexcept {
    return revision != 0 && count <= assets::kMaximumAssetsPerManifest &&
        totalDownloadSize <= assets::kMaximumTotalDownloadSize &&
        (count != 0 || totalDownloadSize == 0);
}

bool WriteAssetDescription(ByteWriter& writer, const assets::AssetDescription& asset) {
    return writer.WriteU32(asset.assetId) &&
        writer.WriteU16(static_cast<std::uint16_t>(asset.type)) &&
        writer.WriteString(asset.logicalName) &&
        writer.WriteString(asset.downloadUrl) &&
        writer.WriteU64(asset.fileSize) &&
        writer.WriteBytes(asset.sha256.data(), asset.sha256.size());
}

DecodeError ReadAssetDescription(
    ByteReader& reader,
    assets::AssetDescription& asset
) noexcept {
    std::uint16_t type{};
    try {
        if (!reader.ReadU32(asset.assetId) || !reader.ReadU16(type) ||
            !reader.ReadString(asset.logicalName, assets::kMaximumLogicalNameLength) ||
            !reader.ReadString(asset.downloadUrl, assets::kMaximumUrlLength) ||
            !reader.ReadU64(asset.fileSize) ||
            !reader.ReadBytes(asset.sha256.data(), asset.sha256.size())) {
            return DecodeError::TruncatedPayload;
        }
    } catch (...) {
        return DecodeError::InvalidField;
    }
    asset.type = static_cast<assets::AssetType>(type);
    return assets::IsAssetDescriptionValid(asset)
        ? DecodeError::None
        : DecodeError::InvalidField;
}

MessageType TypeOf(const Message& message) noexcept {
    return std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Hello>) {
            return MessageType::Hello;
        } else if constexpr (std::is_same_v<T, Welcome>) {
            return MessageType::Welcome;
        } else if constexpr (std::is_same_v<T, Reject>) {
            return MessageType::Reject;
        } else if constexpr (std::is_same_v<T, Ping>) {
            return MessageType::Ping;
        } else if constexpr (std::is_same_v<T, Pong>) {
            return MessageType::Pong;
        } else if constexpr (std::is_same_v<T, ManifestBegin>) {
            return MessageType::ManifestBegin;
        } else if constexpr (std::is_same_v<T, ManifestAsset>) {
            return MessageType::ManifestAsset;
        } else if constexpr (std::is_same_v<T, ManifestEnd>) {
            return MessageType::ManifestEnd;
        } else if constexpr (std::is_same_v<T, AssetReady>) {
            return MessageType::AssetReady;
        } else {
            return MessageType::AssetError;
        }
    }, message);
}

EncodeError EncodePayload(ByteWriter& writer, const Message& message) {
    return std::visit([&writer](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Hello>) {
            if (!IsSampVersionValueValid(static_cast<std::uint16_t>(value.sampVersion)) ||
                !AreCapabilitiesValid(value.capabilities)) {
                return EncodeError::InvalidField;
            }
            if (!WriteRuntimeVersion(writer, value.runtimeVersion) ||
                !writer.WriteU16(value.protocolVersion) ||
                !writer.WriteU16(static_cast<std::uint16_t>(value.sampVersion)) ||
                !writer.WriteU32(value.capabilities)) {
                return EncodeError::OversizedPayload;
            }
        } else if constexpr (std::is_same_v<T, Welcome>) {
            if (!AreCapabilitiesValid(value.negotiatedCapabilities)) {
                return EncodeError::InvalidField;
            }
            if (!writer.WriteU16(value.acceptedProtocolVersion) ||
                !WriteRuntimeVersion(writer, value.serverRuntimeVersion) ||
                !writer.WriteU16(value.serverProtocolVersion) ||
                !writer.WriteU32(value.negotiatedCapabilities)) {
                return EncodeError::OversizedPayload;
            }
        } else if constexpr (std::is_same_v<T, Reject>) {
            const auto reason = static_cast<std::uint16_t>(value.reason);
            if (!IsRejectReasonValid(reason)) {
                return EncodeError::InvalidField;
            }
            if (!writer.WriteU16(reason)) {
                return EncodeError::OversizedPayload;
            }
        } else if constexpr (std::is_same_v<T, Ping> || std::is_same_v<T, Pong>) {
            if (!writer.WriteU64(value.nonce)) {
                return EncodeError::OversizedPayload;
            }
        } else if constexpr (std::is_same_v<T, ManifestBegin> ||
                             std::is_same_v<T, ManifestEnd>) {
            if (!IsManifestBoundaryValid(
                    value.manifestRevision,
                    value.assetCount,
                    value.totalDownloadSize
                )) {
                return EncodeError::InvalidField;
            }
            if (!writer.WriteU64(value.manifestRevision) ||
                !writer.WriteU32(value.assetCount) ||
                !writer.WriteU64(value.totalDownloadSize)) {
                return EncodeError::OversizedPayload;
            }
        } else if constexpr (std::is_same_v<T, ManifestAsset>) {
            if (value.manifestRevision == 0 ||
                value.assetIndex >= assets::kMaximumAssetsPerManifest ||
                !assets::IsAssetDescriptionValid(value.asset)) {
                return EncodeError::InvalidField;
            }
            if (!writer.WriteU64(value.manifestRevision) ||
                !writer.WriteU32(value.assetIndex) ||
                !WriteAssetDescription(writer, value.asset)) {
                return EncodeError::OversizedPayload;
            }
        } else if constexpr (std::is_same_v<T, AssetReady>) {
            if (value.manifestRevision == 0 || value.assetId == 0) {
                return EncodeError::InvalidField;
            }
            if (!writer.WriteU64(value.manifestRevision) ||
                !writer.WriteU32(value.assetId)) {
                return EncodeError::OversizedPayload;
            }
        } else if constexpr (std::is_same_v<T, AssetError>) {
            if (value.manifestRevision == 0 || value.assetId == 0 ||
                !IsAssetErrorReasonValid(static_cast<std::uint16_t>(value.reason))) {
                return EncodeError::InvalidField;
            }
            if (!writer.WriteU64(value.manifestRevision) ||
                !writer.WriteU32(value.assetId) ||
                !writer.WriteU16(static_cast<std::uint16_t>(value.reason))) {
                return EncodeError::OversizedPayload;
            }
        }
        return EncodeError::None;
    }, message);
}

DecodeError DecodePayload(
    MessageType type,
    ByteView payload,
    Message& message
) noexcept {
    ByteReader reader{payload};
    switch (type) {
    case MessageType::Hello: {
        Hello value{};
        std::uint16_t sampVersion{};
        if (!ReadRuntimeVersion(reader, value.runtimeVersion) ||
            !reader.ReadU16(value.protocolVersion) ||
            !reader.ReadU16(sampVersion) ||
            !reader.ReadU32(value.capabilities)) {
            return DecodeError::TruncatedPayload;
        }
        if (!IsSampVersionValueValid(sampVersion) ||
            !AreCapabilitiesValid(value.capabilities)) {
            return DecodeError::InvalidField;
        }
        value.sampVersion = static_cast<SampVersion>(sampVersion);
        message = value;
        break;
    }
    case MessageType::Welcome: {
        Welcome value{};
        if (!reader.ReadU16(value.acceptedProtocolVersion) ||
            !ReadRuntimeVersion(reader, value.serverRuntimeVersion) ||
            !reader.ReadU16(value.serverProtocolVersion) ||
            !reader.ReadU32(value.negotiatedCapabilities)) {
            return DecodeError::TruncatedPayload;
        }
        if (!AreCapabilitiesValid(value.negotiatedCapabilities)) {
            return DecodeError::InvalidField;
        }
        message = value;
        break;
    }
    case MessageType::Reject: {
        std::uint16_t reason{};
        if (!reader.ReadU16(reason)) {
            return DecodeError::TruncatedPayload;
        }
        if (!IsRejectReasonValid(reason)) {
            return DecodeError::InvalidField;
        }
        message = Reject{static_cast<RejectReason>(reason)};
        break;
    }
    case MessageType::Ping: {
        Ping value{};
        if (!reader.ReadU64(value.nonce)) {
            return DecodeError::TruncatedPayload;
        }
        message = value;
        break;
    }
    case MessageType::Pong: {
        Pong value{};
        if (!reader.ReadU64(value.nonce)) {
            return DecodeError::TruncatedPayload;
        }
        message = value;
        break;
    }
    case MessageType::ManifestBegin: {
        ManifestBegin value{};
        if (!reader.ReadU64(value.manifestRevision) ||
            !reader.ReadU32(value.assetCount) ||
            !reader.ReadU64(value.totalDownloadSize)) {
            return DecodeError::TruncatedPayload;
        }
        if (!IsManifestBoundaryValid(
                value.manifestRevision,
                value.assetCount,
                value.totalDownloadSize
            )) {
            return DecodeError::InvalidField;
        }
        message = value;
        break;
    }
    case MessageType::ManifestAsset: {
        ManifestAsset value{};
        if (!reader.ReadU64(value.manifestRevision) ||
            !reader.ReadU32(value.assetIndex)) {
            return DecodeError::TruncatedPayload;
        }
        const auto assetError = ReadAssetDescription(reader, value.asset);
        if (assetError != DecodeError::None) {
            return assetError;
        }
        if (value.manifestRevision == 0 ||
            value.assetIndex >= assets::kMaximumAssetsPerManifest) {
            return DecodeError::InvalidField;
        }
        message = std::move(value);
        break;
    }
    case MessageType::ManifestEnd: {
        ManifestEnd value{};
        if (!reader.ReadU64(value.manifestRevision) ||
            !reader.ReadU32(value.assetCount) ||
            !reader.ReadU64(value.totalDownloadSize)) {
            return DecodeError::TruncatedPayload;
        }
        if (!IsManifestBoundaryValid(
                value.manifestRevision,
                value.assetCount,
                value.totalDownloadSize
            )) {
            return DecodeError::InvalidField;
        }
        message = value;
        break;
    }
    case MessageType::AssetReady: {
        AssetReady value{};
        if (!reader.ReadU64(value.manifestRevision) ||
            !reader.ReadU32(value.assetId)) {
            return DecodeError::TruncatedPayload;
        }
        if (value.manifestRevision == 0 || value.assetId == 0) {
            return DecodeError::InvalidField;
        }
        message = value;
        break;
    }
    case MessageType::AssetError: {
        AssetError value{};
        std::uint16_t reason{};
        if (!reader.ReadU64(value.manifestRevision) ||
            !reader.ReadU32(value.assetId) || !reader.ReadU16(reason)) {
            return DecodeError::TruncatedPayload;
        }
        if (value.manifestRevision == 0 || value.assetId == 0 ||
            !IsAssetErrorReasonValid(reason)) {
            return DecodeError::InvalidField;
        }
        value.reason = static_cast<AssetErrorReason>(reason);
        message = value;
        break;
    }
    }

    return reader.Remaining() == 0 ? DecodeError::None : DecodeError::PayloadLengthMismatch;
}

bool IsKnownMessageType(std::uint16_t value) noexcept {
    return value >= static_cast<std::uint16_t>(MessageType::Hello) &&
        value <= static_cast<std::uint16_t>(MessageType::AssetError);
}

}

EncodeResult EncodePacket(const Message& message, std::uint16_t protocolVersion) {
    if (protocolVersion != kProtocolVersion) {
        return {{}, EncodeError::InvalidField};
    }

    ByteWriter payloadWriter;
    const auto payloadError = EncodePayload(payloadWriter, message);
    if (payloadError != EncodeError::None) {
        return {{}, payloadError};
    }

    auto payload = payloadWriter.Take();
    if (payload.size() > kMaximumPayloadSize ||
        payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return {{}, EncodeError::OversizedPayload};
    }

    ByteWriter packetWriter;
    if (!packetWriter.WriteU32(kMagic) ||
        !packetWriter.WriteU16(protocolVersion) ||
        !packetWriter.WriteU16(static_cast<std::uint16_t>(TypeOf(message))) ||
        !packetWriter.WriteU32(static_cast<std::uint32_t>(payload.size()))) {
        return {{}, EncodeError::OversizedPayload};
    }
    for (const auto byte : payload) {
        if (!packetWriter.WriteU8(byte)) {
            return {{}, EncodeError::OversizedPayload};
        }
    }

    return {packetWriter.Take(), EncodeError::None};
}

DecodeResult DecodePacket(ByteView bytes) noexcept {
    if (bytes.size < kHeaderSize || bytes.data == nullptr) {
        return {std::nullopt, DecodeError::TruncatedHeader};
    }

    ByteReader reader{bytes};
    std::uint32_t magic{};
    std::uint16_t protocolVersion{};
    std::uint16_t messageType{};
    std::uint32_t payloadSize{};
    if (!reader.ReadU32(magic) || !reader.ReadU16(protocolVersion) ||
        !reader.ReadU16(messageType) || !reader.ReadU32(payloadSize)) {
        return {std::nullopt, DecodeError::TruncatedHeader};
    }
    if (magic != kMagic) {
        return {std::nullopt, DecodeError::InvalidMagic};
    }
    if (protocolVersion != kProtocolVersion) {
        return {std::nullopt, DecodeError::UnsupportedProtocolVersion};
    }
    if (payloadSize > kMaximumPayloadSize) {
        return {std::nullopt, DecodeError::OversizedPayload};
    }
    if (payloadSize > reader.Remaining()) {
        return {std::nullopt, DecodeError::TruncatedPayload};
    }
    if (payloadSize != reader.Remaining()) {
        return {std::nullopt, DecodeError::PayloadLengthMismatch};
    }
    if (!IsKnownMessageType(messageType)) {
        return {std::nullopt, DecodeError::UnknownMessageType};
    }

    Packet packet{};
    packet.protocolVersion = protocolVersion;
    const ByteView payload{bytes.data + kHeaderSize, payloadSize};
    const auto error = DecodePayload(
        static_cast<MessageType>(messageType),
        payload,
        packet.message
    );
    if (error != DecodeError::None) {
        return {std::nullopt, error};
    }
    return {std::move(packet), DecodeError::None};
}

bool HasCapability(CapabilityFlags flags, Capability capability) noexcept {
    return (flags & static_cast<CapabilityFlags>(capability)) != 0;
}

bool AreCapabilitiesValid(CapabilityFlags flags) noexcept {
    return (flags & ~kDeclaredCapabilities) == 0;
}

const char* ToString(DecodeError error) noexcept {
    switch (error) {
    case DecodeError::None:
        return "none";
    case DecodeError::TruncatedHeader:
        return "truncated header";
    case DecodeError::InvalidMagic:
        return "invalid magic";
    case DecodeError::UnsupportedProtocolVersion:
        return "unsupported protocol version";
    case DecodeError::OversizedPayload:
        return "oversized payload";
    case DecodeError::TruncatedPayload:
        return "truncated payload";
    case DecodeError::PayloadLengthMismatch:
        return "payload length mismatch";
    case DecodeError::UnknownMessageType:
        return "unknown message type";
    case DecodeError::InvalidField:
        return "invalid field";
    }
    return "unknown decode error";
}

const char* ToString(RejectReason reason) noexcept {
    switch (reason) {
    case RejectReason::UnsupportedProtocol:
        return "unsupported protocol";
    case RejectReason::UnsupportedClientRuntime:
        return "unsupported client runtime";
    case RejectReason::UnsupportedSampBuild:
        return "unsupported SA-MP build";
    case RejectReason::MalformedHandshake:
        return "malformed handshake";
    case RejectReason::UnsupportedCapabilities:
        return "unsupported capabilities";
    }
    return "unknown rejection reason";
}

const char* ToString(AssetErrorReason reason) noexcept {
    switch (reason) {
    case AssetErrorReason::InvalidManifest:
        return "invalid manifest";
    case AssetErrorReason::UnsupportedScheme:
        return "unsupported scheme";
    case AssetErrorReason::NetworkFailure:
        return "network failure";
    case AssetErrorReason::Timeout:
        return "timeout";
    case AssetErrorReason::SizeMismatch:
        return "size mismatch";
    case AssetErrorReason::HashMismatch:
        return "hash mismatch";
    case AssetErrorReason::DiskFailure:
        return "disk failure";
    case AssetErrorReason::InternalError:
        return "internal error";
    }
    return "unknown asset error";
}

}
