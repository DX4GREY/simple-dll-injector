#include "core/auto_inject.hpp"
#include "core/injector.hpp"
#include "utils/process_utils.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace core {

AutoInjectManager::AutoInjectManager() = default;

AutoInjectManager::~AutoInjectManager() {
    Stop();
}

void AutoInjectManager::Start(
    const std::wstring& targetExeName,
    const std::wstring& dllPath,
    StatusCallback onStatus,
    CompletionCallback onComplete
) {
    Stop();

    m_running = true;
    m_workerThread = std::thread(
        &AutoInjectManager::WorkerLoop,
        this,
        targetExeName,
        dllPath,
        std::move(onStatus),
        std::move(onComplete)
    );
}

void AutoInjectManager::Stop() {
    m_running = false;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

bool AutoInjectManager::IsRunning() const {
    return m_running.load();
}

void AutoInjectManager::WorkerLoop(
    std::wstring targetExeName,
    std::wstring dllPath,
    StatusCallback onStatus,
    CompletionCallback onComplete
) {
    if (onStatus) {
        onStatus(L"Auto-Inject active, waiting for target process...");
    }

    while (m_running) {
        DWORD foundPid = utils::FindProcessIdByName(targetExeName);
        if (foundPid != 0) {
            if (onStatus) {
                onStatus(L"Process detected! Waiting for initialization...");
            }

            // Brief delay to allow target module and runtime initialization
            Sleep(1500);

            if (!m_running) break;

            InjectionResult res = PerformInjection(foundPid, dllPath);
            if (onStatus) {
                onStatus(res.message);
            }

            if (res.success) {
                m_running = false;
                if (onComplete) {
                    onComplete(true);
                }
                break;
            }
        }

        Sleep(1000);
    }
}

} // namespace core
