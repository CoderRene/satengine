#pragma once

#include <chrono>

namespace sat {

// Shared by the standalone capture and Lua update on the window thread.
class RenderBenchmark {
public:
    RenderBenchmark(bool enabled, bool captureAssets) {
        m_enabled = enabled;
        m_captureAssets = captureAssets;
    }
    ~RenderBenchmark() { m_enabled = m_captureAssets = false; }
    RenderBenchmark(const RenderBenchmark&) = delete;
    RenderBenchmark& operator=(const RenderBenchmark&) = delete;

    using Clock = std::chrono::steady_clock;
    struct TextureLoads {
        unsigned previews = 0;
        unsigned full = 0;
        Clock::duration read{};
        Clock::duration decode{};
        Clock::duration upload{};
    };
    static void beginFrame() { m_animation = {}; m_textures = {}; }
    [[nodiscard]] static bool captureAssets() { return m_captureAssets; }
    [[nodiscard]] static const TextureLoads& textureLoads() { return m_textures; }
    static void recordTexture(bool preview, bool uploaded, Clock::duration read, Clock::duration decode,
        Clock::duration upload) {
        if (uploaded) { ++(preview ? m_textures.previews : m_textures.full); }
        m_textures.read += read;
        m_textures.decode += decode;
        m_textures.upload += upload;
    }
    [[nodiscard]] static bool enabled() { return m_enabled; }
    static void recordAnimation(Clock::time_point start) { m_animation += Clock::now() - start; }
    [[nodiscard]] static double animationMilliseconds() {
        return std::chrono::duration<double, std::milli>(m_animation).count();
    }

private:
    static thread_local TextureLoads m_textures;
    static inline thread_local bool m_captureAssets = false;
    static inline thread_local bool m_enabled = false;
    static inline thread_local Clock::duration m_animation{};
};

inline thread_local RenderBenchmark::TextureLoads RenderBenchmark::m_textures{};

} // namespace sat
