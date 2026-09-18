#include <custommodel/server/player_capabilities.hpp>

#include <custommodel/version.hpp>

namespace custommodel::server {
namespace {

bool HasPhase2BTransport(SampVersion version) noexcept {
    return version == SampVersion::R1 || version == SampVersion::R3_1;
}

bool IsRuntimeSupported(
    const RuntimeVersion& version,
    const RuntimeVersion& minimum
) noexcept {
    if (version.major != minimum.major) {
        return false;
    }
    if (version.minor != minimum.minor) {
        return version.minor > minimum.minor;
    }
    return version.patch >= minimum.patch;
}

PlayerCapabilityState StateFromHello(
    HandshakeState state,
    const protocol::Hello& hello
) noexcept {
    PlayerCapabilityState result{};
    result.state = state;
    result.runtimeVersion = hello.runtimeVersion;
    result.protocolVersion = hello.protocolVersion;
    result.sampVersion = hello.sampVersion;
    result.declaredCapabilities = hello.capabilities;
    return result;
}

bool SameRuntimeVersion(
    const RuntimeVersion& left,
    const RuntimeVersion& right
) noexcept {
    return left.major == right.major && left.minor == right.minor &&
        left.patch == right.patch;
}

bool SameHello(
    const PlayerCapabilityState& state,
    const protocol::Hello& hello
) noexcept {
    return SameRuntimeVersion(state.runtimeVersion, hello.runtimeVersion) &&
        state.protocolVersion == hello.protocolVersion &&
        state.sampVersion == hello.sampVersion &&
        state.declaredCapabilities == hello.capabilities;
}

}

PlayerCapabilityTracker::PlayerCapabilityTracker(HandshakePolicy policy)
    : policy_(policy) {
    policy_.serverCapabilities &= protocol::kDeclaredCapabilities;
    policy_.requiredCapabilities &= protocol::kDeclaredCapabilities;
}

void PlayerCapabilityTracker::BeginHandshake(std::uint32_t playerId) {
    PlayerCapabilityState state{};
    state.state = HandshakeState::HandshakePending;
    players_[playerId] = state;
}

HandshakeResponse PlayerCapabilityTracker::HandleHello(
    std::uint32_t playerId,
    const protocol::Hello& hello
) {
    const auto current = players_.find(playerId);
    if (current != players_.end() &&
        current->second.state == HandshakeState::Compatible &&
        SameHello(current->second, hello)) {
        return WelcomeFor(current->second);
    }
    if (current != players_.end() &&
        current->second.state == HandshakeState::Rejected &&
        current->second.rejectReason.has_value()) {
        return protocol::Reject{*current->second.rejectReason};
    }
    if (current == players_.end() || current->second.state != HandshakeState::HandshakePending) {
        return Reject(playerId, hello, protocol::RejectReason::MalformedHandshake);
    }
    if (hello.protocolVersion != protocol::kProtocolVersion) {
        return Reject(playerId, hello, protocol::RejectReason::UnsupportedProtocol);
    }
    if (!IsRuntimeSupported(hello.runtimeVersion, policy_.minimumClientRuntime)) {
        return Reject(playerId, hello, protocol::RejectReason::UnsupportedClientRuntime);
    }
    if (FindSampBuild(hello.sampVersion) == nullptr ||
        !HasPhase2BTransport(hello.sampVersion)) {
        return Reject(playerId, hello, protocol::RejectReason::UnsupportedSampBuild);
    }
    if (!protocol::AreCapabilitiesValid(hello.capabilities)) {
        return Reject(playerId, hello, protocol::RejectReason::MalformedHandshake);
    }

    auto state = StateFromHello(HandshakeState::Compatible, hello);
    state.negotiatedCapabilities = hello.capabilities & policy_.serverCapabilities;
    if ((state.negotiatedCapabilities & policy_.requiredCapabilities) !=
        policy_.requiredCapabilities) {
        return Reject(
            playerId,
            hello,
            protocol::RejectReason::UnsupportedCapabilities
        );
    }
    players_[playerId] = state;

    return WelcomeFor(state);
}

protocol::Reject PlayerCapabilityTracker::RejectPlayer(
    std::uint32_t playerId,
    protocol::RejectReason reason
) {
    PlayerCapabilityState state{};
    state.state = HandshakeState::Rejected;
    state.rejectReason = reason;
    players_[playerId] = state;
    return protocol::Reject{reason};
}

void PlayerCapabilityTracker::RemovePlayer(std::uint32_t playerId) noexcept {
    players_.erase(playerId);
}

void PlayerCapabilityTracker::Reset() noexcept {
    players_.clear();
}

PlayerCapabilityState PlayerCapabilityTracker::GetState(
    std::uint32_t playerId
) const noexcept {
    const auto player = players_.find(playerId);
    return player == players_.end() ? PlayerCapabilityState{} : player->second;
}

std::size_t PlayerCapabilityTracker::TrackedPlayerCount() const noexcept {
    return players_.size();
}

protocol::Reject PlayerCapabilityTracker::Reject(
    std::uint32_t playerId,
    const protocol::Hello& hello,
    protocol::RejectReason reason
) {
    auto state = StateFromHello(HandshakeState::Rejected, hello);
    state.rejectReason = reason;
    players_[playerId] = state;
    return protocol::Reject{reason};
}

protocol::Welcome PlayerCapabilityTracker::WelcomeFor(
    const PlayerCapabilityState& state
) const noexcept {
    return {
        protocol::kProtocolVersion,
        policy_.serverRuntimeVersion,
        protocol::kProtocolVersion,
        state.negotiatedCapabilities,
    };
}

}
