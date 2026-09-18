#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <windows.h>

namespace custommodel::core {

struct ModuleInfo {
    HMODULE handle{};
    std::uintptr_t base{};
    std::size_t imageSize{};
    std::uint32_t entryPointRva{};
    bool isPe32{};

    bool Contains(std::uintptr_t address, std::size_t length = 1) const noexcept;
    bool IsAccessible(std::uintptr_t address, std::size_t length = 1) const noexcept;
    bool IsExecutable(std::uintptr_t address) const noexcept;
    std::optional<std::uintptr_t> ResolveRva(
        std::uintptr_t rva,
        std::size_t length = 1
    ) const noexcept;
};

struct ModuleLookupResult {
    std::optional<ModuleInfo> module;
    const char* error{};

    explicit operator bool() const noexcept {
        return module.has_value();
    }
};

ModuleLookupResult FindLoadedModule(const wchar_t* name) noexcept;

}
