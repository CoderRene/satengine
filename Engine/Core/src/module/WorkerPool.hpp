#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

namespace sat {

// Single submitting thread. The callback context is borrowed until parallelFor returns.
class WorkerPool {
public:
    using Task = void (*)(void*, std::size_t) noexcept;

    WorkerPool() = default;
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void parallelFor(std::size_t count, void* context, Task task);

private:
    void run();
    void work() const noexcept;
    void shutdown();

    static constexpr std::size_t kChunkSize = 4;
    std::mutex m_mutex;
    std::condition_variable m_ready, m_done;
    std::vector<std::jthread> m_workers;
    mutable std::atomic<std::size_t> m_next{0};
    std::size_t m_count = 0, m_available = 0, m_pending = 0;
    void* m_context = nullptr;
    Task m_task = nullptr;
    bool m_initialized = false, m_stopping = false;
};

} // namespace sat
