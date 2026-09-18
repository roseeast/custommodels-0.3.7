#include <custommodel/core/logger.hpp>
#include <custommodel/core/runtime.hpp>

#include <windows.h>

static_assert(sizeof(void*) == 4, "CustomModel.asi must be compiled for Windows x86");

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(instance);
        if (!custommodel::core::ScheduleInitialization(instance)) {
            OutputDebugStringA("[CustomModel] failed to schedule initialization\n");
        }
        break;
    case DLL_PROCESS_DETACH:
        custommodel::core::GetRuntime().RequestShutdownFromLoaderLock();
        custommodel::core::GetLogger().ShutdownFromLoaderLock();
        break;
    default:
        break;
    }

    return TRUE;
}
