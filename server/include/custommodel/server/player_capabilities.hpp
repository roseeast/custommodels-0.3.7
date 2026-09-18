#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <variant>

#include <custommodel/protocol.hpp>

namespace custommodel::server {

enum class HandshakeState : std::uint8_t {
    Unknown,
    HandshakePending,
    Compatible,
    Rejected,
};

struct PlayerCapabilityState {
    HandshakeState state{HandshakeState::Unknown};
    RuntimeVersion runtimeVersion{};
    std::uint16_t protocolVersion{};
    SampVersion sampVersion{SampVersion::Unknown};
    protocol::CapabilityFlags declaredCapabilities{};
    protocol::CapabilityFlags negotiatedCapabilities{};
    std::optional<protocol::RejectReason> rejectReason;
};

struct HandshakePolicy {
    RuntimeVersion minimumClientRuntime{kRuntimeVersion};
    RuntimeVersion serverRuntimeVersion{kRuntimeVersion};
    protocol::CapabilityFlags serverCapabilities{};
    protocol::CapabilityFlags requiredCapabilities{};
};

using HandshakeResponse = std::variant<protocol::Welcome, protocol::Reject>;

class PlayerCapabilityTracker final {
public:
    explicit PlayerCapabilityTracker(HandshakePolicy policy = {});

    void BeginHandshake(std::uint32_t playerId);
    HandshakeResponse HandleHello(std::uint32_t playerId, const protocol::Hello& hello);
    protocol::Reject RejectPlayer(std::uint32_t playerId, protocol::RejectReason reason);
    void RemovePlayer(std::uint32_t playerId) noexcept;
    void Reset() noexcept;

    PlayerCapabilityState GetState(std::uint32_t playerId) const noexcept;
    std::size_t TrackedPlayerCount() const noexcept;

private:
    protocol::Reject Reject(
        std::uint32_t playerId,
        const protocol::Hello& hello,
        protocol::RejectReason reason
    );
    protocol::Welcome WelcomeFor(const PlayerCapabilityState& state) const noexcept;

    HandshakePolicy policy_;
    std::unordered_map<std::uint32_t, PlayerCapabilityState> players_;
};

}
