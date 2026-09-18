#include <custommodel/samp/compatibility.hpp>

#include <array>

namespace custommodel::samp {
namespace {

constexpr std::array<SampCompatibility, 5> kCompatibility{{
    {SampVersion::R1, {0x21A0F8, 0x1A40, 0}},
    {SampVersion::R2, {}},
    {SampVersion::R3_1, {0x26E8DC, 0x1A40, 0}},
    {SampVersion::R4, {}},
    {SampVersion::R4Family, {}},
}};

}

bool SampCompatibility::HasMapping(AddressMapping mapping) const noexcept {
    switch (mapping) {
    case AddressMapping::NetGame:
        return addresses.netGamePointerRva != 0;
    case AddressMapping::GetRakClient:
        return addresses.getRakClientRva != 0;
    case AddressMapping::RpcHandler:
        return addresses.rpcHandlerRva != 0;
    }

    return false;
}

bool SampCompatibility::HasAllMappings(
    std::initializer_list<AddressMapping> mappings
) const noexcept {
    for (const auto mapping : mappings) {
        if (!HasMapping(mapping)) {
            return false;
        }
    }

    return true;
}

const SampCompatibility* FindCompatibility(SampVersion version) noexcept {
    for (const auto& compatibility : kCompatibility) {
        if (compatibility.version == version) {
            return &compatibility;
        }
    }

    return nullptr;
}

}
