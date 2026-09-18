#include <custommodel/transport_frame.hpp>

namespace custommodel::transport_frame {
namespace {

bool HasProtocolMagic(protocol::ByteView bytes) noexcept {
    return bytes.data != nullptr && bytes.size >= sizeof(protocol::kMagic) &&
        bytes.data[0] == 'C' && bytes.data[1] == 'M' &&
        bytes.data[2] == 'O' && bytes.data[3] == 'D';
}

}

InspectedFrame Inspect(protocol::ByteView frame) noexcept {
    if (frame.data == nullptr || frame.size <= kPrefixSize ||
        frame.data[0] != kRakNetPacketId) {
        return {};
    }

    const protocol::ByteView protocolBytes{
        frame.data + kPrefixSize,
        frame.size - kPrefixSize,
    };
    return InspectProtocolPayload(protocolBytes);
}

InspectedFrame InspectProtocolPayload(protocol::ByteView protocolBytes) noexcept {
    if (!HasProtocolMagic(protocolBytes)) {
        return {};
    }

    return {FrameKind::CustomModel, protocolBytes};
}

std::optional<std::vector<std::uint8_t>> Encode(protocol::ByteView protocolBytes) {
    if (!HasProtocolMagic(protocolBytes) ||
        protocolBytes.size > protocol::kMaximumPacketSize) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> frame;
    frame.reserve(kPrefixSize + protocolBytes.size);
    frame.push_back(kRakNetPacketId);
    frame.insert(
        frame.end(),
        protocolBytes.data,
        protocolBytes.data + protocolBytes.size
    );
    return frame;
}

}
