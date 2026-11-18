#pragma once

#include <chrono>

namespace sat {

// Shared by the standalone capture and Lua update on the window thread.
class RenderBenchmark {
public:
    explicit RenderBenchmark(bool enabled) { m_enabled = enabled; }
    ~RenderBenchmark() { m_enabled = false; }
    RenderBenchmark(const RenderBenchmark&) = delete;
    RenderBenchmark& operator=(const RenderBenchmark&) = delete;

    using Clock = std::chrono::steady_clock;
    static void beginFrame() { m_animation = {}; }
    [[nodiscard]] static bool enabled() { return m_enabled; }
    static void recordAnimation(Clock::time_point start) { m_animation += Clock::now() - start; }
    [[nodiscard]] static double animationMilliseconds() {
        return std::chrono::duration<double, std::milli>(m_animation).count();
    }

private:
    static inline thread_local bool m_enabled = false;
    static inline thread_local Clock::duration m_animation{};
};

} // namespace sat
