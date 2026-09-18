#pragma once

#include <limits>

#include <custommodel/protocol/transport.hpp>

namespace custommodel::samp {

struct RakNetRawSendBuffer {
    const char* data{};
    int byteLength{};

    explicit operator bool() const noexcept {
        return data != nullptr && byteLength > 0;
    }
};

inline RakNetRawSendBuffer MakeRakNetRawSendBuffer(protocol::ByteView bytes) noexcept {
    if (bytes.data == nullptr || bytes.size == 0 ||
        bytes.size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {};
    }

    return {
        reinterpret_cast<const char*>(bytes.data),
        static_cast<int>(bytes.size),
    };
}

client_protocol::ClientTransport* FindRakNetTransport(SampVersion version) noexcept;

}
