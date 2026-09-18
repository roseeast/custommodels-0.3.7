#pragma once

#include <custommodel/server/asset_publication.hpp>
#include <custommodel/server/asset_registry.hpp>
#include <custommodel/server/handshake_endpoint.hpp>
#include <custommodel/server/player_capabilities.hpp>
#include <custommodel/server/transport.hpp>

#include <bitstream.hpp>
#include <sdk.hpp>

namespace custommodel::server::openmp {

class OpenMpTransport final : public ServerTransport {
public:
    void Attach(ICore* core) noexcept;

    bool Initialize(IncomingHandler handler, void* context) noexcept override;
    void Shutdown() noexcept override;
    bool Send(std::uint32_t playerId, protocol::ByteView bytes) noexcept override;
    bool DispatchIncoming(
        std::uint32_t playerId,
        protocol::ByteView bytes
    ) noexcept override;
    bool IsAvailable() const noexcept override;

private:
    ICore* core_{};
    IncomingHandler handler_{};
    void* context_{};
};

class CustomModelComponent final : public IComponent,
                                   public PlayerConnectEventHandler,
                                   public NetworkInEventHandler,
                                   public SingleNetworkInEventHandler {
public:
    PROVIDE_UID(0x65CB3534EA506BF0);

    StringView componentName() const override;
    SemanticVersion componentVersion() const override;
    void onLoad(ICore* core) override;
    void free() override;
    void reset() override;

    void onPlayerConnect(IPlayer& player) override;
    void onPlayerDisconnect(IPlayer& player, PeerDisconnectReason reason) override;
    bool onReceivePacket(IPlayer& player, int id, NetworkBitStream& stream) override;
    bool onReceive(IPlayer& player, NetworkBitStream& stream) override;

private:
    static HandshakePolicy MakePolicy() noexcept;
    static void DispatchProtocol(
        std::uint32_t playerId,
        protocol::ByteView bytes,
        void* context
    ) noexcept;

    void LogDispatch(std::uint32_t playerId, DispatchResult result) const noexcept;
    void LogAssetDispatch(
        std::uint32_t playerId,
        AssetDispatchResult result,
        const protocol::Message& message
    ) const noexcept;
    void LogPacketDebug(
        const char* stage,
        std::uint32_t playerId,
        const NetworkBitStream& stream
    ) const noexcept;
    DispatchResult DispatchMalformed(std::uint32_t playerId) noexcept;

    ICore* core_{};
    bool registered_{};
    PlayerCapabilityTracker tracker_{MakePolicy()};
    OpenMpTransport transport_;
    HandshakeEndpoint endpoint_{tracker_, transport_};
    AssetRegistry registry_;
    AssetPublication assets_{registry_, transport_};
    DispatchResult lastDispatch_{DispatchResult::SendFailed};
    AssetDispatchResult lastAssetDispatch_{AssetDispatchResult::Ignored};
};

}
