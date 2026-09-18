#include <custommodel/protocol/handshake.hpp>

#include <variant>

namespace custommodel::client_protocol {

ClientHandshake::ClientHandshake(HandshakeConfiguration configuration)
    : configuration_(configuration) {
    configuration_.capabilities &= protocol::kDeclaredCapabilities;
}

void ClientHandshake::OnConnected() noexcept {
    if (state_ == HandshakeState::Disconnected) {
        state_ = HandshakeState::Connected;
        welcome_.reset();
        rejection_.reset();
    }
}

void ClientHandshake::OnDisconnected() noexcept {
    state_ = HandshakeState::Disconnected;
    welcome_.reset();
    rejection_.reset();
}

std::optional<std::vector<std::uint8_t>> ClientHandshake::TakeHello() {
    if (state_ != HandshakeState::Connected) {
        return std::nullopt;
    }

    const protocol::Hello hello{
        configuration_.runtimeVersion,
        protocol::kProtocolVersion,
        configuration_.sampVersion,
        configuration_.capabilities,
    };
    auto encoded = protocol::EncodePacket(hello);
    if (!encoded) {
        return std::nullopt;
    }

    state_ = HandshakeState::HelloSent;
    return std::move(encoded.bytes);
}

IncomingResult ClientHandshake::HandleIncoming(protocol::ByteView bytes) noexcept {
    const auto decoded = protocol::DecodePacket(bytes);
    if (!decoded || !decoded.packet.has_value()) {
        return IncomingResult::Malformed;
    }

    if (state_ != HandshakeState::HelloSent) {
        return IncomingResult::Ignored;
    }

    if (const auto* welcome = std::get_if<protocol::Welcome>(&decoded.packet->message)) {
        if (welcome->acceptedProtocolVersion != protocol::kProtocolVersion ||
            welcome->serverProtocolVersion != protocol::kProtocolVersion ||
            !protocol::AreCapabilitiesValid(welcome->negotiatedCapabilities) ||
            (welcome->negotiatedCapabilities & ~configuration_.capabilities) != 0) {
            return IncomingResult::Malformed;
        }

        welcome_ = *welcome;
        state_ = HandshakeState::WelcomeReceived;
        return IncomingResult::WelcomeReceived;
    }

    if (const auto* reject = std::get_if<protocol::Reject>(&decoded.packet->message)) {
        rejection_ = *reject;
        state_ = HandshakeState::Rejected;
        return IncomingResult::Rejected;
    }

    return IncomingResult::Ignored;
}

bool ClientHandshake::ConfirmWelcome() noexcept {
    if (state_ != HandshakeState::WelcomeReceived) {
        return false;
    }

    state_ = HandshakeState::Compatible;
    return true;
}

HandshakeState ClientHandshake::State() const noexcept {
    return state_;
}

const std::optional<protocol::Welcome>& ClientHandshake::Welcome() const noexcept {
    return welcome_;
}

const std::optional<protocol::Reject>& ClientHandshake::Rejection() const noexcept {
    return rejection_;
}

}
