#pragma once

#include <cstdint>
#include <initializer_list>

#include <custommodel/version.hpp>

namespace custommodel::samp {

struct SampAddresses {
    std::uintptr_t netGamePointerRva{};
    std::uintptr_t getRakClientRva{};
    std::uintptr_t rpcHandlerRva{};
};

enum class AddressMapping : std::uint8_t {
    NetGame,
    GetRakClient,
    RpcHandler,
};

struct SampCompatibility {
    SampVersion version{SampVersion::Unknown};
    SampAddresses addresses{};

    bool HasMapping(AddressMapping mapping) const noexcept;
    bool HasAllMappings(std::initializer_list<AddressMapping> mappings) const noexcept;
};

const SampCompatibility* FindCompatibility(SampVersion version) noexcept;

}
