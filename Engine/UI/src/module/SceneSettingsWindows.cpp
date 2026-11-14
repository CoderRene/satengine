#include "SceneSettingsWindows.hpp"

#include "CursorWrap.hpp"
#include "PhysicsInspector.hpp"

#include "AssetData.hpp"
#include "AudioTypes.hpp"
#include "Project.hpp"
#include "Scene.hpp"

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace sat {

void drawSceneSettingsWindows(bool& showLighting, bool& showAudio, bool& showPhysics,
    Scene& scene, const Project& project, bool readOnly, bool playMode, std::size_t physicsWorldCount) {
    if (showLighting) {
        if (ImGui::Begin("Lighting Settings", &showLighting)) {
            auto& settings = scene.lightingSettings;
            ImGui::BeginDisabled(readOnly);
            bool changed = ImGui::ColorEdit3("Ambient Color", settings.ambientColor);
            if (ImGui::CollapsingHeader("Bloom")) {
                auto& bloom = settings.bloom;
                changed |= ImGui::Checkbox("Enable Bloom", &bloom.enabled);
                ImGui::BeginDisabled(!bloom.enabled);
                changed |= dragFloat("Threshold", &bloom.threshold, 0.05f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                changed |= ImGui::SliderFloat("Soft Knee", &bloom.softKnee, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                changed |= dragFloat("Intensity", &bloom.intensity, 0.01f, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                changed |= dragFloat("Radius (px)", &bloom.radius, 0.25f, 0.0f, 64.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                changed |= ImGui::ColorEdit3("Tint", bloom.tint);
                changed |= ImGui::SliderFloat("Highlight Clamp", &bloom.highlightClamp, 0, 65504, "%.2f",
                    ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
                changed |= ImGui::Combo("Sampling Quality", &bloom.quality, "Low (9 taps)\0Medium (25 taps)\0High (49 taps)\0");
                ImGui::EndDisabled();
                if (ImGui::Button("Reset Bloom Defaults")) { bloom = {}; changed = true; }
            }
            changed |= ImGui::Checkbox("Directional Shadows", &settings.directionalShadows);
            if (settings.directionalShadows) {
                constexpr int kResolutions[] = {512, 1024, 2048, 4096};
                int resolution = settings.shadowResolution == 512 ? 0 : settings.shadowResolution == 1024 ? 1 :
                    settings.shadowResolution == 4096 ? 3 : 2;
                if (ImGui::Combo("Cascade Resolution", &resolution, "512\0" "1024\0" "2048\0" "4096\0")) {
                    settings.shadowResolution = kResolutions[resolution];
                    changed = true;
                }
                changed |= dragFloat("Shadow Distance", &settings.shadowDistance, 0.5f,
                    10.0f, 1000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
                changed |= ImGui::SliderInt("Cascades", &settings.shadowCascadeCount, 1, 4, "%d",
                    ImGuiSliderFlags_AlwaysClamp);
                changed |= ImGui::SliderFloat("Split Distribution", &settings.shadowSplitLambda, 0, 1, "%.2f",
                    ImGuiSliderFlags_AlwaysClamp);
            }
            if (ImGui::BeginCombo("Environment Map", settings.environmentPath.empty() ? "None" : settings.environmentPath.c_str())) {
                if (ImGui::Selectable("None", settings.environmentPath.empty())) {
                    settings.environmentPath.clear();
                    changed = true;
                }
                std::error_code ec;
                for (auto it = std::filesystem::recursive_directory_iterator(project.root() / "Assets", ec);
                     !ec && it != std::filesystem::recursive_directory_iterator{}; it.increment(ec)) {
                    if (!it->is_regular_file(ec)) { continue; }
                    const auto encoded = it->path().lexically_relative(project.root()).generic_u8string();
                    const std::string path(encoded.begin(), encoded.end());
                    if (isTextureAssetPath(path) && ImGui::Selectable(path.c_str(), path == settings.environmentPath)) {
                        settings.environmentPath = path;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            changed |= dragFloat("Environment Intensity", &settings.environmentIntensity, 0.01f,
                0.0f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            changed |= dragFloat("Ambient Intensity", &settings.ambientIntensity, 0.01f,
                0.0f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            if (changed) { scene.markEdited(); }
            ImGui::EndDisabled();
        }
        ImGui::End();
    }
    if (showAudio) {
        if (ImGui::Begin("Audio Settings", &showAudio)) {
            auto& settings = scene.audioSettings;
            ImGui::BeginDisabled(readOnly);
            bool changed = dragFloat("Master Volume", &settings.masterVolume, 0.01f, 0.0f, 16.0f,
                "%.2f", ImGuiSliderFlags_AlwaysClamp);
            constexpr const char* kBusNames[] = {"SFX", "Music", "UI"};
            for (std::size_t index = 0; index < kAudioBusCount; ++index) {
                ImGui::PushID(static_cast<int>(index));
                ImGui::TextUnformatted(kBusNames[index]);
                changed |= dragFloat("Volume", &settings.busVolumes[index], 0.01f, 0.0f, 16.0f,
                    "%.2f", ImGuiSliderFlags_AlwaysClamp);
                changed |= ImGui::Checkbox("Follow Time Scale", &settings.followTimeScale[index]);
                ImGui::PopID();
            }
            changed |= dragFloat("Speed of Sound (m/s)", &settings.speedOfSound, 1.0f, 1.0f, 10000.0f,
                "%.1f", ImGuiSliderFlags_AlwaysClamp);
            constexpr std::uint32_t kMinVoices = 1;
            constexpr std::uint32_t kMinVirtual = 0;
            changed |= ImGui::SliderScalar("Mixed Voices", ImGuiDataType_U32, &settings.maxVoices,
                &kMinVoices, &kMaxAudioVoices);
            changed |= ImGui::SliderScalar("Virtual Voices", ImGuiDataType_U32, &settings.maxVirtualVoices,
                &kMinVirtual, &kMaxAudioVoices);
            if (changed) { scene.markEdited(); }
            ImGui::EndDisabled();
        }
        ImGui::End();
    }
    if (showPhysics) {
        if (ImGui::Begin("Physics Settings", &showPhysics)) {
            ImGui::BeginDisabled(readOnly);
            if (drawPhysicsSettings(scene.physicsSettings)) { scene.markEdited(); }
            ImGui::EndDisabled();
            if (playMode) { ImGui::Text("Active local worlds: %zu", physicsWorldCount); }
        }
        ImGui::End();
    }
}

} // namespace sat
