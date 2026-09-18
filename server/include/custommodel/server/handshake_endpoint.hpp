#pragma once

#include <cstdint>

#include <custommodel/server/player_capabilities.hpp>
#include <custommodel/server/transport.hpp>

namespace custommodel::server {

enum class DispatchResult : std::uint8_t {
    Unrelated,
    WelcomeSent,
    RejectSent,
    SendFailed,
};

class HandshakeEndpoint final {
public:
    HandshakeEndpoint(PlayerCapabilityTracker& tracker, ServerTransport& transport) noexcept;

    void OnPlayerConnected(std::uint32_t playerId);
    void OnPlayerDisconnected(std::uint32_t playerId) noexcept;
    DispatchResult DispatchIncoming(
        std::uint32_t playerId,
        protocol::ByteView frame
    );
    DispatchResult DispatchProtocolPayload(
        std::uint32_t playerId,
        protocol::ByteView protocolBytes
    );
    DispatchResult RejectMalformed(std::uint32_t playerId);

private:
    DispatchResult DispatchClaimedProtocol(
        std::uint32_t playerId,
        protocol::ByteView protocolBytes
    );
    DispatchResult Reject(
        std::uint32_t playerId,
        protocol::RejectReason reason
    );
    DispatchResult SendResponse(
        std::uint32_t playerId,
        const HandshakeResponse& response
    );

    PlayerCapabilityTracker& tracker_;
    ServerTransport& transport_;
};

}
