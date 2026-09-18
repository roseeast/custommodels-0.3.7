#pragma once

#include <cstdint>

#include <custommodel/protocol.hpp>

namespace custommodel::server {

using IncomingHandler = void(*)(
    std::uint32_t playerId,
    protocol::ByteView bytes,
    void* context
) noexcept;

class ServerTransport {
public:
    virtual ~ServerTransport() = default;

    virtual bool Initialize(IncomingHandler handler, void* context) noexcept = 0;
    virtual void Shutdown() noexcept = 0;
    virtual bool Send(std::uint32_t playerId, protocol::ByteView bytes) noexcept = 0;
    virtual bool DispatchIncoming(
        std::uint32_t playerId,
        protocol::ByteView bytes
    ) noexcept = 0;
    virtual bool IsAvailable() const noexcept = 0;
};

ServerTransport& UnavailableServerTransport() noexcept;

}
