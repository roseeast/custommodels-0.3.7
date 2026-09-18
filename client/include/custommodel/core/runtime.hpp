#pragma once

#include <atomic>
#include <string_view>

#include <windows.h>

namespace custommodel::core {

enum class RuntimeState {
    Dormant,
    Initializing,
    Ready,
    Disabled,
    ShuttingDown,
};

class ClientRuntime final {
public:
    void Initialize(HMODULE module) noexcept;
    void RequestShutdownFromLoaderLock() noexcept;
    RuntimeState State() const noexcept;

private:
    void InitializeImpl(HMODULE module);
    void Disable(std::string_view reason) noexcept;

    std::atomic<RuntimeState> state_{RuntimeState::Dormant};
};

ClientRuntime& GetRuntime() noexcept;
bool ScheduleInitialization(HMODULE module) noexcept;

}
