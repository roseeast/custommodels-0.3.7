#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include <custommodel/server/asset_registry.hpp>
#include <custommodel/server/transport.hpp>

namespace custommodel::server {

enum class AssetSessionState : std::uint8_t {
    Unknown,
    HandshakePending,
    Compatible,
    ManifestSending,
    ManifestComplete,
    AssetsReady,
    Rejected,
};

enum class AssetDispatchResult : std::uint8_t {
    Ignored,
    ReadyRecorded,
    ErrorRecorded,
    InvalidOrder,
    InvalidAsset,
};

enum class ManifestSendResult : std::uint8_t {
    Sent,
    AlreadySent,
    InvalidState,
    SendFailed,
};

struct PlayerAssetState {
    AssetSessionState state{AssetSessionState::Unknown};
    std::uint64_t manifestRevision{};
    std::unordered_set<std::uint32_t> expectedAssets;
    std::unordered_set<std::uint32_t> readyAssets;
    std::unordered_map<std::uint32_t, protocol::AssetErrorReason> failedAssets;
};

class AssetPublication final {
public:
    AssetPublication(const AssetRegistry& registry, ServerTransport& transport) noexcept;

    void OnPlayerConnected(std::uint32_t playerId);
    void OnPlayerDisconnected(std::uint32_t playerId) noexcept;
    ManifestSendResult OnPlayerCompatible(std::uint32_t playerId);
    AssetDispatchResult HandleIncoming(
        std::uint32_t playerId,
        const protocol::Message& message
    );
    void Reset() noexcept;

    PlayerAssetState GetState(std::uint32_t playerId) const;

private:
    bool Send(std::uint32_t playerId, const protocol::Message& message);

    const AssetRegistry& registry_;
    ServerTransport& transport_;
    std::unordered_map<std::uint32_t, PlayerAssetState> players_;
};

}
