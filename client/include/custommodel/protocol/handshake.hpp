#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <custommodel/protocol.hpp>

namespace custommodel::client_protocol {

enum class HandshakeState : std::uint8_t {
    Disconnected,
    Connected,
    HelloSent,
    WelcomeReceived,
    Compatible,
    Rejected,
};

enum class IncomingResult : std::uint8_t {
    Ignored,
    Malformed,
    WelcomeReceived,
    Rejected,
};

struct HandshakeConfiguration {
    RuntimeVersion runtimeVersion{kRuntimeVersion};
    SampVersion sampVersion{SampVersion::Unknown};
    protocol::CapabilityFlags capabilities{};
};

class ClientHandshake final {
public:
    explicit ClientHandshake(HandshakeConfiguration configuration);

    void OnConnected() noexcept;
    void OnDisconnected() noexcept;
    std::optional<std::vector<std::uint8_t>> TakeHello();
    IncomingResult HandleIncoming(protocol::ByteView bytes) noexcept;
    bool ConfirmWelcome() noexcept;

    HandshakeState State() const noexcept;
    const std::optional<protocol::Welcome>& Welcome() const noexcept;
    const std::optional<protocol::Reject>& Rejection() const noexcept;

private:
    HandshakeConfiguration configuration_;
    HandshakeState state_{HandshakeState::Disconnected};
    std::optional<protocol::Welcome> welcome_;
    std::optional<protocol::Reject> rejection_;
};

}
