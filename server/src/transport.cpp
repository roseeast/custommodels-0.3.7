#include <custommodel/server/transport.hpp>

namespace custommodel::server {
namespace {

class UnavailableTransport final : public ServerTransport {
public:
    bool Initialize(IncomingHandler, void*) noexcept override {
        return false;
    }

    void Shutdown() noexcept override {}

    bool Send(std::uint32_t, protocol::ByteView) noexcept override {
        return false;
    }

    bool DispatchIncoming(std::uint32_t, protocol::ByteView) noexcept override {
        return false;
    }

    bool IsAvailable() const noexcept override {
        return false;
    }
};

UnavailableTransport g_unavailableTransport;

}

ServerTransport& UnavailableServerTransport() noexcept {
    return g_unavailableTransport;
}

}
