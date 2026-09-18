#pragma once

#include <custommodel/protocol.hpp>
#include <custommodel/version.hpp>

namespace custommodel::client_protocol {

using IncomingHandler = void(*)(protocol::ByteView bytes, void* context) noexcept;

enum class ConnectionEvent : std::uint8_t {
    Connected,
    Disconnected,
};

using ConnectionHandler = void(*)(ConnectionEvent event, void* context) noexcept;
using PumpHandler = void(*)(void* context) noexcept;

struct TransportCallbacks {
    IncomingHandler incoming{};
    ConnectionHandler connection{};
    PumpHandler pump{};
    void* context{};
};

class ClientTransport {
public:
    virtual ~ClientTransport() = default;

    virtual bool Initialize(TransportCallbacks callbacks) noexcept = 0;
    virtual void Shutdown() noexcept = 0;
    virtual bool Send(protocol::ByteView bytes) noexcept = 0;
    virtual bool DispatchIncoming(protocol::ByteView bytes) noexcept = 0;
    virtual bool IsAvailable() const noexcept = 0;
};

ClientTransport& SelectSampTransport(SampVersion version) noexcept;

}
