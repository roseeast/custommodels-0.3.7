#include "component.hpp"

#include <custommodel/transport_frame.hpp>
#include <custommodel/version.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <variant>

namespace custommodel::server::openmp {
namespace {

constexpr auto kRequiredCapabilities =
    static_cast<protocol::CapabilityFlags>(protocol::Capability::Dff) |
    static_cast<protocol::CapabilityFlags>(protocol::Capability::Txd) |
    static_cast<protocol::CapabilityFlags>(protocol::Capability::Sha256Cache);
constexpr std::size_t kPreviewByteLimit = 16;

}

void OpenMpTransport::Attach(ICore* core) noexcept {
    core_ = core;
}

bool OpenMpTransport::Initialize(IncomingHandler handler, void* context) noexcept {
    handler_ = handler;
    context_ = context;
    return core_ != nullptr && handler_ != nullptr;
}

void OpenMpTransport::Shutdown() noexcept {
    handler_ = nullptr;
    context_ = nullptr;
    core_ = nullptr;
}

bool OpenMpTransport::Send(
    std::uint32_t playerId,
    protocol::ByteView bytes
) noexcept {
    if (core_ == nullptr || bytes.data == nullptr || bytes.size == 0 ||
        bytes.size > transport_frame::kMaximumFrameSize ||
        playerId > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    auto* player = core_->getPlayers().get(static_cast<int>(playerId));
    if (player == nullptr) {
        return false;
    }

    std::array<std::uint8_t, transport_frame::kMaximumFrameSize> copy{};
    std::copy_n(bytes.data, bytes.size, copy.data());

    try {
        return player->sendPacket(
            Span<std::uint8_t>(copy.data(), bytes.size * 8U),
            OrderingChannel_Reliable
        );
    } catch (...) {
        return false;
    }
}

bool OpenMpTransport::DispatchIncoming(
    std::uint32_t playerId,
    protocol::ByteView bytes
) noexcept {
    if (!IsAvailable()) {
        return false;
    }
    handler_(playerId, bytes, context_);
    return true;
}

bool OpenMpTransport::IsAvailable() const noexcept {
    return core_ != nullptr && handler_ != nullptr;
}

HandshakePolicy CustomModelComponent::MakePolicy() noexcept {
    HandshakePolicy policy{};
    policy.serverCapabilities = protocol::kDeclaredCapabilities;
    policy.requiredCapabilities = kRequiredCapabilities;
    return policy;
}

StringView CustomModelComponent::componentName() const {
    return "CustomModel";
}

SemanticVersion CustomModelComponent::componentVersion() const {
    return SemanticVersion(
        static_cast<std::uint8_t>(kRuntimeVersion.major),
        static_cast<std::uint8_t>(kRuntimeVersion.minor),
        static_cast<std::uint8_t>(kRuntimeVersion.patch),
        0
    );
}

void CustomModelComponent::onLoad(ICore* core) {
    core_ = core;
    if (core_->getNetworkBitStreamVersion() != NetworkBitStream::Version) {
        core_->logLn(
            LogLevel::Error,
            "[CustomModel] incompatible open.mp NetworkBitStream version"
        );
        return;
    }

    transport_.Attach(core);
    if (!transport_.Initialize(&CustomModelComponent::DispatchProtocol, this)) {
        core_->logLn(LogLevel::Error, "[CustomModel] server transport initialization failed");
        return;
    }

    const std::filesystem::path manifestPath{"custommodel-assets.cfg"};
    std::error_code manifestPathError;
    if (std::filesystem::exists(manifestPath, manifestPathError)) {
        const auto loaded = LoadManifestConfig(manifestPath, registry_);
        if (!loaded) {
            core_->logLn(
                LogLevel::Error,
                "[CustomModel] manifest configuration error at line %u: %s",
                static_cast<unsigned>(loaded.errorLine),
                loaded.error.c_str()
            );
            transport_.Shutdown();
            return;
        }
        core_->printLn(
            "[CustomModel] loaded %u manifest assets",
            static_cast<unsigned>(loaded.loadedAssets)
        );
    } else if (manifestPathError) {
        core_->logLn(LogLevel::Error, "[CustomModel] manifest path check failed");
        transport_.Shutdown();
        return;
    } else {
        core_->printLn("[CustomModel] no manifest configuration; publishing an empty manifest");
    }

    core_->getPlayers().getPlayerConnectDispatcher().addEventHandler(this);
    for (auto* network : core_->getNetworks()) {
        network->getInEventDispatcher().addEventHandler(this, EventPriority_Highest);
    }
    core_->addPerPacketInEventHandler<transport_frame::kRakNetPacketId>(this);
    for (auto* network : core_->getNetworks()) {
        core_->printLn(
            "[CustomModel:debug] network=%d packet 0x5E handler count=%u global incoming handler count=%u",
            static_cast<int>(network->getNetworkType()),
            static_cast<unsigned>(
                network->getPerPacketInEventDispatcher().count(
                    transport_frame::kRakNetPacketId
                )
            ),
            static_cast<unsigned>(network->getInEventDispatcher().count())
        );
    }
    registered_ = true;
    core_->printLn("[CustomModel] server transport initialized");
}

void CustomModelComponent::free() {
    if (core_ != nullptr && registered_) {
        core_->removePerPacketInEventHandler<transport_frame::kRakNetPacketId>(this);
        for (auto* network : core_->getNetworks()) {
            network->getInEventDispatcher().removeEventHandler(this);
        }
        core_->getPlayers().getPlayerConnectDispatcher().removeEventHandler(this);
    }
    registered_ = false;
    tracker_.Reset();
    assets_.Reset();
    transport_.Shutdown();
    core_ = nullptr;
    delete this;
}

void CustomModelComponent::reset() {
    tracker_.Reset();
    assets_.Reset();
}

void CustomModelComponent::onPlayerConnect(IPlayer& player) {
    if (player.getID() >= 0) {
        endpoint_.OnPlayerConnected(static_cast<std::uint32_t>(player.getID()));
        assets_.OnPlayerConnected(static_cast<std::uint32_t>(player.getID()));
    }
}

void CustomModelComponent::onPlayerDisconnect(
    IPlayer& player,
    PeerDisconnectReason
) {
    if (player.getID() >= 0) {
        endpoint_.OnPlayerDisconnected(static_cast<std::uint32_t>(player.getID()));
        assets_.OnPlayerDisconnected(static_cast<std::uint32_t>(player.getID()));
    }
}

bool CustomModelComponent::onReceivePacket(
    IPlayer& player,
    int id,
    NetworkBitStream& stream
) {
    if (id == transport_frame::kRakNetPacketId && player.getID() >= 0) {
        LogPacketDebug(
            "carrier observed before per-packet dispatch",
            static_cast<std::uint32_t>(player.getID()),
            stream
        );
    }
    return true;
}

bool CustomModelComponent::onReceive(IPlayer& player, NetworkBitStream& stream) {
    constexpr int kMagicBits = 32;
    if (player.getID() < 0) {
        return true;
    }
    const auto playerId = static_cast<std::uint32_t>(player.getID());
    LogPacketDebug("packet handler invoked", playerId, stream);

    const auto unreadBits = stream.GetNumberOfUnreadBits();
    const auto readOffset = stream.GetReadOffset();
    if (unreadBits < kMagicBits || readOffset < 0 || (readOffset % 8) != 0 ||
        stream.GetData() == nullptr) {
        return true;
    }

    const auto* payloadData = stream.GetData() + readOffset / 8;
    const protocol::ByteView magic{payloadData, 4};
    if (transport_frame::InspectProtocolPayload(magic).kind ==
        transport_frame::FrameKind::Unrelated) {
        return true;
    }

    if ((unreadBits % 8) != 0 ||
        unreadBits / 8 > static_cast<int>(protocol::kMaximumPacketSize)) {
        LogDispatch(playerId, DispatchMalformed(playerId));
        return false;
    }

    const protocol::ByteView protocolBytes{
        payloadData,
        static_cast<std::size_t>(unreadBits / 8),
    };
    const auto decoded = protocol::DecodePacket(protocolBytes);
    const bool isHello = decoded.packet.has_value() &&
        std::holds_alternative<protocol::Hello>(decoded.packet->message);
    const bool isAssetReport = decoded.packet.has_value() &&
        (std::holds_alternative<protocol::AssetReady>(decoded.packet->message) ||
         std::holds_alternative<protocol::AssetError>(decoded.packet->message));
    if (isHello) {
        core_->printLn(
            "[CustomModel] Hello received from player %u",
            static_cast<unsigned>(playerId)
        );
    }

    lastDispatch_ = DispatchResult::SendFailed;
    lastAssetDispatch_ = AssetDispatchResult::Ignored;
    if (!transport_.DispatchIncoming(playerId, protocolBytes)) {
        core_->logLn(
            LogLevel::Error,
            "[CustomModel] failed to dispatch frame from player %u",
            static_cast<unsigned>(playerId)
        );
        return false;
    }
    if (isHello) {
        LogDispatch(playerId, lastDispatch_);
        if (lastDispatch_ == DispatchResult::WelcomeSent) {
            const auto result = assets_.OnPlayerCompatible(playerId);
            if (result == ManifestSendResult::Sent) {
                const auto state = assets_.GetState(playerId);
                core_->printLn(
                    "[CustomModel] manifest revision %llu sent to player %u (%u assets)",
                    static_cast<unsigned long long>(state.manifestRevision),
                    static_cast<unsigned>(playerId),
                    static_cast<unsigned>(state.expectedAssets.size())
                );
            } else if (result == ManifestSendResult::SendFailed) {
                core_->logLn(
                    LogLevel::Error,
                    "[CustomModel] manifest send failed for player %u",
                    static_cast<unsigned>(playerId)
                );
            }
        }
    } else if (isAssetReport) {
        LogAssetDispatch(playerId, lastAssetDispatch_, decoded.packet->message);
    }
    return false;
}

void CustomModelComponent::DispatchProtocol(
    std::uint32_t playerId,
    protocol::ByteView bytes,
    void* context
) noexcept {
    auto& component = *static_cast<CustomModelComponent*>(context);
    try {
        const auto decoded = protocol::DecodePacket(bytes);
        if (!decoded || !decoded.packet.has_value()) {
            component.lastDispatch_ = DispatchResult::SendFailed;
            component.lastAssetDispatch_ = AssetDispatchResult::InvalidOrder;
        } else if (std::holds_alternative<protocol::Hello>(decoded.packet->message)) {
            component.lastDispatch_ = component.endpoint_.DispatchProtocolPayload(playerId, bytes);
        } else {
            component.lastAssetDispatch_ = component.assets_.HandleIncoming(
                playerId,
                decoded.packet->message
            );
        }
    } catch (...) {
        component.lastDispatch_ = DispatchResult::SendFailed;
        if (component.core_ != nullptr) {
            component.core_->logLn(
                LogLevel::Error,
                "[CustomModel] handshake processing failed for player %u",
                static_cast<unsigned>(playerId)
            );
        }
    }
}

void CustomModelComponent::LogAssetDispatch(
    std::uint32_t playerId,
    AssetDispatchResult result,
    const protocol::Message& message
) const noexcept {
    if (core_ == nullptr) {
        return;
    }
    if (result == AssetDispatchResult::ReadyRecorded) {
        const auto* ready = std::get_if<protocol::AssetReady>(&message);
        if (ready != nullptr) {
            core_->printLn(
                "[CustomModel] asset %u ready for player %u",
                static_cast<unsigned>(ready->assetId),
                static_cast<unsigned>(playerId)
            );
        }
    } else if (result == AssetDispatchResult::ErrorRecorded) {
        const auto* error = std::get_if<protocol::AssetError>(&message);
        if (error != nullptr) {
            core_->printLn(
                "[CustomModel] asset %u failed for player %u: %s",
                static_cast<unsigned>(error->assetId),
                static_cast<unsigned>(playerId),
                protocol::ToString(error->reason)
            );
        }
    } else if (result == AssetDispatchResult::InvalidOrder ||
               result == AssetDispatchResult::InvalidAsset) {
        core_->logLn(
            LogLevel::Warning,
            "[CustomModel] invalid asset status from player %u",
            static_cast<unsigned>(playerId)
        );
    }
}

void CustomModelComponent::LogPacketDebug(
    const char* stage,
    std::uint32_t playerId,
    const NetworkBitStream& stream
) const noexcept {
    if (core_ == nullptr) {
        return;
    }

    const auto remainingBits = stream.GetNumberOfUnreadBits();
    const auto readOffset = stream.GetReadOffset();
    const auto remainingBytes = remainingBits > 0 ? (remainingBits - 1) / 8 + 1 : 0;
    std::array<char, kPreviewByteLimit * 3> preview{};
    std::size_t previewLength{};

    if (stream.GetData() != nullptr && remainingBits > 0 && (readOffset % 8) == 0) {
        const auto count = std::min<std::size_t>(
            static_cast<std::size_t>(remainingBytes),
            kPreviewByteLimit
        );
        const auto* data = stream.GetData() + readOffset / 8;
        for (std::size_t index = 0; index < count; ++index) {
            const auto written = std::snprintf(
                preview.data() + previewLength,
                preview.size() - previewLength,
                index == 0 ? "%02X" : " %02X",
                static_cast<unsigned>(data[index])
            );
            if (written <= 0) {
                break;
            }
            previewLength += static_cast<std::size_t>(written);
        }
    }

    core_->printLn("[CustomModel:debug] %s", stage);
    core_->printLn("[CustomModel:debug] player=%u", static_cast<unsigned>(playerId));
    core_->printLn("[CustomModel:debug] remaining bits=%d", remainingBits);
    core_->printLn("[CustomModel:debug] remaining bytes=%d", remainingBytes);
    core_->printLn("[CustomModel:debug] read offset=%d", readOffset);
    core_->printLn(
        "[CustomModel:debug] preview=%s",
        previewLength == 0 ? "<none>" : preview.data()
    );
}

void CustomModelComponent::LogDispatch(
    std::uint32_t playerId,
    DispatchResult result
) const noexcept {
    if (core_ == nullptr) {
        return;
    }

    const auto state = tracker_.GetState(playerId);
    if (result == DispatchResult::WelcomeSent) {
        core_->printLn(
            "[CustomModel] build %s accepted for player %u",
            ToString(state.sampVersion),
            static_cast<unsigned>(playerId)
        );
        core_->printLn(
            "[CustomModel] Welcome sent to player %u",
            static_cast<unsigned>(playerId)
        );
    } else if (result == DispatchResult::RejectSent) {
        core_->printLn(
            "[CustomModel] Reject sent to player %u: %s",
            static_cast<unsigned>(playerId),
            state.rejectReason.has_value()
                ? protocol::ToString(*state.rejectReason)
                : "unknown reason"
        );
    } else if (result == DispatchResult::SendFailed) {
        core_->logLn(
            LogLevel::Error,
            "[CustomModel] handshake response send failed for player %u",
            static_cast<unsigned>(playerId)
        );
    }
}

DispatchResult CustomModelComponent::DispatchMalformed(std::uint32_t playerId) noexcept {
    try {
        return endpoint_.RejectMalformed(playerId);
    } catch (...) {
        return DispatchResult::SendFailed;
    }
}

}
