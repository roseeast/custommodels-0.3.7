#include <custommodel/server/asset_publication.hpp>

#include <custommodel/transport_frame.hpp>

#include <variant>

namespace custommodel::server {

AssetPublication::AssetPublication(
    const AssetRegistry& registry,
    ServerTransport& transport
) noexcept : registry_(registry), transport_(transport) {}

void AssetPublication::OnPlayerConnected(std::uint32_t playerId) {
    PlayerAssetState state{};
    state.state = AssetSessionState::HandshakePending;
    players_[playerId] = std::move(state);
}

void AssetPublication::OnPlayerDisconnected(std::uint32_t playerId) noexcept {
    players_.erase(playerId);
}

ManifestSendResult AssetPublication::OnPlayerCompatible(std::uint32_t playerId) {
    const auto found = players_.find(playerId);
    if (found == players_.end()) {
        return ManifestSendResult::InvalidState;
    }
    auto& state = found->second;
    if (state.state == AssetSessionState::ManifestComplete ||
        state.state == AssetSessionState::AssetsReady) {
        return ManifestSendResult::AlreadySent;
    }
    if (state.state != AssetSessionState::HandshakePending &&
        state.state != AssetSessionState::Compatible) {
        return ManifestSendResult::InvalidState;
    }

    const auto manifest = registry_.BuildManifest();
    state = {};
    state.state = AssetSessionState::Compatible;
    state.manifestRevision = manifest.revision;
    for (const auto& asset : manifest.assets) {
        state.expectedAssets.insert(asset.assetId);
    }

    state.state = AssetSessionState::ManifestSending;
    const protocol::ManifestBegin begin{
        manifest.revision,
        static_cast<std::uint32_t>(manifest.assets.size()),
        manifest.totalDownloadSize,
    };
    if (!Send(playerId, begin)) {
        state.state = AssetSessionState::Rejected;
        return ManifestSendResult::SendFailed;
    }

    for (std::size_t index = 0; index < manifest.assets.size(); ++index) {
        if (!Send(playerId, protocol::ManifestAsset{
                manifest.revision,
                static_cast<std::uint32_t>(index),
                manifest.assets[index],
            })) {
            state.state = AssetSessionState::Rejected;
            return ManifestSendResult::SendFailed;
        }
    }

    const protocol::ManifestEnd end{
        manifest.revision,
        static_cast<std::uint32_t>(manifest.assets.size()),
        manifest.totalDownloadSize,
    };
    if (!Send(playerId, end)) {
        state.state = AssetSessionState::Rejected;
        return ManifestSendResult::SendFailed;
    }

    state.state = manifest.assets.empty()
        ? AssetSessionState::AssetsReady
        : AssetSessionState::ManifestComplete;
    return ManifestSendResult::Sent;
}

AssetDispatchResult AssetPublication::HandleIncoming(
    std::uint32_t playerId,
    const protocol::Message& message
) {
    const auto found = players_.find(playerId);
    if (found == players_.end()) {
        return AssetDispatchResult::InvalidOrder;
    }
    auto& state = found->second;
    if (state.state != AssetSessionState::ManifestComplete &&
        state.state != AssetSessionState::AssetsReady) {
        return AssetDispatchResult::InvalidOrder;
    }

    if (const auto* ready = std::get_if<protocol::AssetReady>(&message)) {
        if (ready->manifestRevision != state.manifestRevision ||
            state.expectedAssets.find(ready->assetId) == state.expectedAssets.end()) {
            return AssetDispatchResult::InvalidAsset;
        }
        state.failedAssets.erase(ready->assetId);
        state.readyAssets.insert(ready->assetId);
        if (state.readyAssets.size() == state.expectedAssets.size()) {
            state.state = AssetSessionState::AssetsReady;
        }
        return AssetDispatchResult::ReadyRecorded;
    }

    if (const auto* error = std::get_if<protocol::AssetError>(&message)) {
        if (error->manifestRevision != state.manifestRevision ||
            state.expectedAssets.find(error->assetId) == state.expectedAssets.end()) {
            return AssetDispatchResult::InvalidAsset;
        }
        state.readyAssets.erase(error->assetId);
        state.failedAssets[error->assetId] = error->reason;
        state.state = AssetSessionState::ManifestComplete;
        return AssetDispatchResult::ErrorRecorded;
    }

    return AssetDispatchResult::Ignored;
}

void AssetPublication::Reset() noexcept {
    players_.clear();
}

PlayerAssetState AssetPublication::GetState(std::uint32_t playerId) const {
    const auto found = players_.find(playerId);
    return found == players_.end() ? PlayerAssetState{} : found->second;
}

bool AssetPublication::Send(
    std::uint32_t playerId,
    const protocol::Message& message
) {
    const auto encoded = protocol::EncodePacket(message);
    if (!encoded) {
        return false;
    }
    const auto framed = transport_frame::Encode({encoded.bytes.data(), encoded.bytes.size()});
    return framed.has_value() &&
        transport_.Send(playerId, {framed->data(), framed->size()});
}

}
