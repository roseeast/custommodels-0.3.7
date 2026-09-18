#include <custommodel/server/handshake_endpoint.hpp>

#include <custommodel/transport_frame.hpp>

#include <variant>

namespace custommodel::server {

HandshakeEndpoint::HandshakeEndpoint(
    PlayerCapabilityTracker& tracker,
    ServerTransport& transport
) noexcept : tracker_(tracker), transport_(transport) {}

void HandshakeEndpoint::OnPlayerConnected(std::uint32_t playerId) {
    tracker_.BeginHandshake(playerId);
}

void HandshakeEndpoint::OnPlayerDisconnected(std::uint32_t playerId) noexcept {
    tracker_.RemovePlayer(playerId);
}

DispatchResult HandshakeEndpoint::DispatchIncoming(
    std::uint32_t playerId,
    protocol::ByteView frame
) {
    const auto inspected = transport_frame::Inspect(frame);
    if (inspected.kind == transport_frame::FrameKind::Unrelated) {
        return DispatchResult::Unrelated;
    }

    return DispatchClaimedProtocol(playerId, inspected.protocolBytes);
}

DispatchResult HandshakeEndpoint::DispatchProtocolPayload(
    std::uint32_t playerId,
    protocol::ByteView protocolBytes
) {
    const auto inspected = transport_frame::InspectProtocolPayload(protocolBytes);
    if (inspected.kind == transport_frame::FrameKind::Unrelated) {
        return DispatchResult::Unrelated;
    }

    return DispatchClaimedProtocol(playerId, inspected.protocolBytes);
}

DispatchResult HandshakeEndpoint::DispatchClaimedProtocol(
    std::uint32_t playerId,
    protocol::ByteView protocolBytes
) {
    if (protocolBytes.size > protocol::kMaximumPacketSize) {
        return Reject(playerId, protocol::RejectReason::MalformedHandshake);
    }

    const auto decoded = protocol::DecodePacket(protocolBytes);
    if (!decoded || !decoded.packet.has_value()) {
        const auto reason = decoded.error == protocol::DecodeError::UnsupportedProtocolVersion
            ? protocol::RejectReason::UnsupportedProtocol
            : protocol::RejectReason::MalformedHandshake;
        return Reject(playerId, reason);
    }

    const auto* hello = std::get_if<protocol::Hello>(&decoded.packet->message);
    if (hello == nullptr) {
        return Reject(playerId, protocol::RejectReason::MalformedHandshake);
    }

    return SendResponse(playerId, tracker_.HandleHello(playerId, *hello));
}

DispatchResult HandshakeEndpoint::RejectMalformed(std::uint32_t playerId) {
    return Reject(playerId, protocol::RejectReason::MalformedHandshake);
}

DispatchResult HandshakeEndpoint::Reject(
    std::uint32_t playerId,
    protocol::RejectReason reason
) {
    return SendResponse(playerId, tracker_.RejectPlayer(playerId, reason));
}

DispatchResult HandshakeEndpoint::SendResponse(
    std::uint32_t playerId,
    const HandshakeResponse& response
) {
    const auto encoded = std::visit([](const auto& message) {
        return protocol::EncodePacket(message);
    }, response);
    if (!encoded) {
        return DispatchResult::SendFailed;
    }

    const auto frame = transport_frame::Encode({encoded.bytes.data(), encoded.bytes.size()});
    if (!frame.has_value() || !transport_.Send(playerId, {frame->data(), frame->size()})) {
        return DispatchResult::SendFailed;
    }

    return std::holds_alternative<protocol::Welcome>(response)
        ? DispatchResult::WelcomeSent
        : DispatchResult::RejectSent;
}

}
