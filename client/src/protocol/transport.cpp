#include <custommodel/protocol/transport.hpp>

#if defined(_WIN32)
#include <custommodel/samp/raknet_transport.hpp>
#endif

namespace custommodel::client_protocol {
namespace {

class UnavailableTransport final : public ClientTransport {
public:
    bool Initialize(TransportCallbacks) noexcept override {
        return false;
    }

    void Shutdown() noexcept override {}

    bool Send(protocol::ByteView) noexcept override {
        return false;
    }

    bool DispatchIncoming(protocol::ByteView) noexcept override {
        return false;
    }

    bool IsAvailable() const noexcept override {
        return false;
    }
};

UnavailableTransport g_unavailableTransport;

}

ClientTransport& SelectSampTransport(SampVersion version) noexcept {
#if defined(_WIN32)
    if (auto* transport = samp::FindRakNetTransport(version)) {
        return *transport;
    }
#else
    (void)version;
#endif
    return g_unavailableTransport;
}

}
