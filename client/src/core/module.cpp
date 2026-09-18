#include <custommodel/core/module.hpp>

#include <custommodel/pe_image.hpp>

#include <limits>

namespace custommodel::core {

bool ModuleInfo::Contains(std::uintptr_t address, std::size_t length) const noexcept {
    if (base == 0 || imageSize == 0 || address < base) {
        return false;
    }

    const auto offset = address - base;
    if (offset >= imageSize) {
        return false;
    }

    return length != 0 && length <= imageSize - offset;
}

bool ModuleInfo::IsAccessible(std::uintptr_t address, std::size_t length) const noexcept {
    if (!Contains(address, length)) {
        return false;
    }

    const auto end = address + length;
    auto current = address;
    while (current < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(current), &memory, sizeof(memory)) == 0) {
            return false;
        }

        if (memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return false;
        }

        if (reinterpret_cast<std::uintptr_t>(memory.AllocationBase) != base) {
            return false;
        }

        const auto regionBase = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        if (memory.RegionSize > std::numeric_limits<std::uintptr_t>::max() - regionBase) {
            return false;
        }

        const auto next = regionBase + memory.RegionSize;
        if (next <= current) {
            return false;
        }
        current = next;
    }

    return true;
}

bool ModuleInfo::IsExecutable(std::uintptr_t address) const noexcept {
    if (!Contains(address)) {
        return false;
    }

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &memory, sizeof(memory)) == 0 ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        reinterpret_cast<std::uintptr_t>(memory.AllocationBase) != base) {
        return false;
    }

    const DWORD protection = memory.Protect & 0xFFU;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

std::optional<std::uintptr_t> ModuleInfo::ResolveRva(
    std::uintptr_t rva,
    std::size_t length
) const noexcept {
    if (rva == 0 || rva > std::numeric_limits<std::uintptr_t>::max() - base) {
        return std::nullopt;
    }

    const auto address = base + rva;
    if (!Contains(address, length)) {
        return std::nullopt;
    }

    return address;
}

ModuleLookupResult FindLoadedModule(const wchar_t* name) noexcept {
    if (name == nullptr || *name == L'\0') {
        return {{}, "module name is empty"};
    }

    const auto handle = GetModuleHandleW(name);
    if (handle == nullptr) {
        return {{}, "module is not loaded"};
    }

    const auto base = reinterpret_cast<std::uintptr_t>(handle);
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(base), &memory, sizeof(memory)) == 0) {
        return {{}, "VirtualQuery failed for the module header"};
    }

    if (memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return {{}, "module header memory is not accessible"};
    }

    if (memory.AllocationBase != handle) {
        return {{}, "module header does not belong to the expected allocation"};
    }

    const auto regionBase = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    if (base < regionBase || base - regionBase >= memory.RegionSize) {
        return {{}, "module base is outside its header memory region"};
    }

    const auto headerSize = memory.RegionSize - (base - regionBase);
    const auto parsed = ParsePeImage(
        reinterpret_cast<const std::uint8_t*>(base),
        headerSize
    );
    if (!parsed) {
        return {{}, ToString(parsed.error)};
    }

    const auto& image = *parsed.image;
    if (image.sizeOfImage > std::numeric_limits<std::uintptr_t>::max() - base) {
        return {{}, "module image range overflows the address space"};
    }

    ModuleInfo result{};
    result.handle = handle;
    result.base = base;
    result.imageSize = image.sizeOfImage;
    result.entryPointRva = image.entryPointRva;
    result.isPe32 = image.kind == PeImageKind::Pe32;
    return {result, nullptr};
}

}
