#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <custommodel/protocol.hpp>

namespace custommodel::transport_frame {

inline constexpr std::uint8_t kRakNetPacketId = 0x5E;
inline constexpr std::size_t kPrefixSize = 1;
inline constexpr std::size_t kMaximumFrameSize =
    kPrefixSize + protocol::kMaximumPacketSize;

enum class FrameKind : std::uint8_t {
    Unrelated,
    CustomModel,
};

struct InspectedFrame {
    FrameKind kind{FrameKind::Unrelated};
    protocol::ByteView protocolBytes{};
};

InspectedFrame Inspect(protocol::ByteView frame) noexcept;
InspectedFrame InspectProtocolPayload(protocol::ByteView protocolBytes) noexcept;
std::optional<std::vector<std::uint8_t>> Encode(protocol::ByteView protocolBytes);

inline InspectedFrame Inspect(const std::vector<std::uint8_t>& frame) noexcept {
    return Inspect({frame.data(), frame.size()});
}

}
