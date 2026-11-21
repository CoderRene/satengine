#include "Application.hpp"

#include "AnimationSystem.hpp"

#include "GameUi.hpp"
#include "Renderer.hpp"
#include "SceneRenderData.hpp"
#include "Window.hpp"
#include "module/RenderBenchmark.hpp"

#include <GLFW/glfw3.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Psapi.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <iostream>
#include <thread>

namespace sat {
namespace {

std::filesystem::path benchmarkPath(const wchar_t* variable) {
    wchar_t* value = nullptr;
    std::size_t count = 0;
    if (_wdupenv_s(&value, &count, variable) != 0) { return {}; }
    const std::unique_ptr<wchar_t, decltype(&std::free)> environment(value, &std::free);
    return environment ? std::filesystem::path(environment.get()) : std::filesystem::path{};
}

} // namespace

int Application::run() {
    const auto started = std::chrono::steady_clock::now();
    const auto loadingPath = benchmarkPath(L"SAT_LOADING_BENCHMARK");
    Window window(1280, 720, "SatGame");
    if (window.nativeHandle() == nullptr) {
        return 1;
    }
    Renderer renderer(window.nativeHandle());
    if (!renderer.valid()) {
        return 1;
    }

    GameUi ui(renderer.uiRenderInterface(),
        [this](std::string_view path, std::string& bytes, std::string& error) {
            return m_assetReader ? m_assetReader(path, bytes, error) : AssetReadResult::Missing;
        });
    struct ResetUi {
        GameUi*& pointer;
        ~ResetUi() { pointer = nullptr; }
    } reset{m_gameUi};
    m_gameUi = &ui;

    m_input = window.input();
    m_stopRequested = false;
    onStart();
    if (m_animation) { m_animation->enableOcclusionFeedback(); }
    renderer.setAssetReader(m_assetReader);
    renderer.setShaderReader(m_shaderReader);
    if (!m_stopRequested && !renderer.initializeShaders()) {
        onStop();
        return 1;
    }
    constexpr unsigned kWarmupFrames = 120;
    constexpr unsigned kSampleFrames = 600;
    const auto capturePath = benchmarkPath(L"SAT_RENDER_BENCHMARK");
    const RenderBenchmark benchmark(!capturePath.empty() || !loadingPath.empty(), !loadingPath.empty());
    std::ofstream capture;
    unsigned captured = 0;
    if (!capturePath.empty()) {
        capture.open(capturePath);
        if (!capture) {
            std::cerr << "Cannot open render benchmark capture " << capturePath << '\n';
            onStop();
            return 1;
        }
        capture << "frame_ms,gpu_frame_ms,draw_calls,set_pass_calls,cpu_animation_ms,cpu_scene_sync_ms\n";
    }
    std::ofstream loading;
    unsigned loadingFrame = 0;
    if (!loadingPath.empty()) {
        loading.open(loadingPath);
        if (!loading) {
            std::cerr << "Cannot open loading benchmark capture " << loadingPath << '\n';
            onStop();
            return 1;
        }
        loading << "frame,elapsed_ms,frame_ms,peak_working_set_bytes,gpu_shadow_ms,gpu_transparent_ms,"
            "texture_previews,texture_full,texture_read_ms,texture_decode_ms,texture_upload_ms\n";
    }
    SceneRenderData renderData;
    int previousWidth = 0, previousHeight = 0;
    std::size_t reportedAssetErrors = 0;
    auto previous = std::chrono::steady_clock::now();
    while (!m_stopRequested && !window.shouldClose()) {
        window.pollEvents();
        m_input = window.input();
        if (window.shouldClose()) {
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        const float deltaSeconds = std::chrono::duration<float>(now - previous).count();
        previous = now;
        int width = 0;
        int height = 0;
        auto* nativeWindow = static_cast<GLFWwindow*>(window.nativeHandle());
        glfwGetFramebufferSize(nativeWindow, &width, &height);
        if (width != previousWidth || height != previousHeight) {
            if (m_animation) { m_animation->invalidateOcclusion(); }
            previousWidth = width;
            previousHeight = height;
        }
        int clientWidth = 0;
        int clientHeight = 0;
        glfwGetWindowSize(nativeWindow, &clientWidth, &clientHeight);
        const bool visible = width > 0 && height > 0 && clientWidth > 0 && clientHeight > 0;
        const bool focused = visible && glfwGetWindowAttrib(nativeWindow, GLFW_FOCUSED) == GLFW_TRUE;
        ui.setViewport(width, height);
        ui.processInput(m_input, 0.0f, 0.0f,
            visible ? static_cast<float>(width) / static_cast<float>(clientWidth) : 1.0f,
            visible ? static_cast<float>(height) / static_cast<float>(clientHeight) : 1.0f,
            focused, focused);
        if (capture.is_open() || loading.is_open()) { RenderBenchmark::beginFrame(); }
        onUpdate(deltaSeconds);
        if (m_stopRequested) { break; }
        ui.update();
        if (width < 1 || height < 1) {
            // Avoid rendering/presenting a minimized window or spinning at full speed.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        renderer.newFrame();
        renderData.update(m_scene, width, height, m_animation);
        auto drawList = renderData.drawList();
        drawList.captureGpuStats = loading.is_open();
        drawList.animationOcclusion = renderData.hasCamera() ? m_animation : nullptr;
        if (!renderer.renderWindow(renderData.camera(), drawList)) {
            onStop();
            return 1;
        }
        const auto& assetErrors = renderer.assetErrors();
        for (; reportedAssetErrors < assetErrors.size(); ++reportedAssetErrors) {
            std::cerr << assetErrors[reportedAssetErrors] << '\n';
        }
        renderer.renderUi(ui, UiTarget::Window);
        renderer.present();
        if (loading.is_open()) {
            const auto presented = std::chrono::steady_clock::now();
            const auto milliseconds = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
            PROCESS_MEMORY_COUNTERS memory{};
            memory.cb = sizeof(memory);
            const auto stats = renderer.gameViewStats();
            loading << ++loadingFrame << ',' << milliseconds(presented - started) << ',' << milliseconds(presented - now) << ',';
            if (K32GetProcessMemoryInfo(GetCurrentProcess(), &memory, memory.cb)) { loading << memory.PeakWorkingSetSize; }
            loading << ',';
            if (stats.gpuShadowMs) { loading << *stats.gpuShadowMs; }
            loading << ',';
            if (stats.gpuTransparentMs) { loading << *stats.gpuTransparentMs; }
            const auto& textures = RenderBenchmark::textureLoads();
            loading << ',' << textures.previews << ',' << textures.full << ',' << milliseconds(textures.read) <<
                ',' << milliseconds(textures.decode) << ',' << milliseconds(textures.upload) << '\n';
            if (loadingFrame == 180) {
                loading.close();
                if (!capture.is_open()) { break; }
            }
        }
        if (capture.is_open() && ++captured > kWarmupFrames) {
            const auto stats = renderer.gameViewStats();
            capture << static_cast<double>(deltaSeconds) * 1000.0 << ',';
            if (stats.gpuFrameTimeMs) { capture << *stats.gpuFrameTimeMs; }
            capture << ',' << stats.drawCalls << ',' << stats.setPassCalls << ','
                << RenderBenchmark::animationMilliseconds() << ',' << stats.cpuSceneSyncMs << '\n';
            if (captured == kWarmupFrames + kSampleFrames) { break; }
        }
    }
    onStop();
    return 0;
}

} // namespace sat
