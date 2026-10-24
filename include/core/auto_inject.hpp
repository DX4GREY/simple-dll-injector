#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>

namespace core {

class AutoInjectManager {
public:
    using StatusCallback = std::function<void(const std::wstring&)>;
    using CompletionCallback = std::function<void(bool success)>;

    AutoInjectManager();
    ~AutoInjectManager();

    // Starts background monitoring for the target process
    void Start(
        const std::wstring& targetExeName,
        const std::wstring& dllPath,
        StatusCallback onStatus,
        CompletionCallback onComplete
    );

    // Stops background monitoring
    void Stop();

    // Returns whether monitoring is currently active
    bool IsRunning() const;

private:
    void WorkerLoop(
        std::wstring targetExeName,
        std::wstring dllPath,
        StatusCallback onStatus,
        CompletionCallback onComplete
    );

    std::atomic<bool> m_running{ false };
    std::thread m_workerThread;
};

} // namespace core
