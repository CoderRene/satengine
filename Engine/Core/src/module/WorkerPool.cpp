#include "WorkerPool.hpp"

#include <algorithm>
#include <system_error>

namespace sat {

WorkerPool::~WorkerPool() { shutdown(); }

void WorkerPool::shutdown() {
    {
        const std::scoped_lock lock(m_mutex);
        m_stopping = true;
    }
    m_ready.notify_all();
    m_workers.clear();
}

void WorkerPool::parallelFor(std::size_t count, void* context, Task task) {
    if (!m_initialized) {
        m_initialized = true;
        const auto threadCount = std::max(1u, std::thread::hardware_concurrency()) - 1;
        try {
            m_workers.reserve(threadCount);
            for (unsigned i = 0; i < threadCount; ++i) { m_workers.emplace_back([this] { run(); }); }
        } catch (const std::system_error&) {
            // Retain the workers that started. The caller also drains the queue.
        }
    }
    const auto participants = count == 0 ? 0 : std::min(m_workers.size(), (count - 1) / kChunkSize);
    if (participants == 0) {
        for (std::size_t i = 0; i < count; ++i) { task(context, i); }
        return;
    }
    {
        const std::scoped_lock lock(m_mutex);
        m_count = count;
        m_context = context;
        m_task = task;
        m_next.store(0, std::memory_order_relaxed);
        m_available = m_pending = participants;
    }
    for (std::size_t i = 0; i < participants; ++i) { m_ready.notify_one(); }
    work();
    std::unique_lock lock(m_mutex);
    m_done.wait(lock, [this] { return m_pending == 0; });
}

void WorkerPool::run() {
    std::unique_lock lock(m_mutex);
    while (true) {
        m_ready.wait(lock, [this] { return m_stopping || m_available != 0; });
        if (m_stopping) { return; }
        --m_available;
        lock.unlock();
        work();
        lock.lock();
        if (--m_pending == 0) { m_done.notify_one(); }
    }
}

void WorkerPool::work() const noexcept {
    // The mutex publishes each job and joins all consumers. This counter only claims ranges.
    for (auto first = m_next.fetch_add(kChunkSize, std::memory_order_relaxed); first < m_count;
        first = m_next.fetch_add(kChunkSize, std::memory_order_relaxed)) {
        const auto end = std::min(first + kChunkSize, m_count);
        for (auto index = first; index < end; ++index) { m_task(m_context, index); }
    }
}

} // namespace sat
