#pragma once

#include "AssetImportSettings.hpp"
#include "AudioSystem.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sat {

class Project;
class Renderer;

class ContentPanel {
public:
    ContentPanel(Project& project, Renderer& renderer);
    ~ContentPanel();

    ContentPanel(const ContentPanel&) = delete;
    ContentPanel& operator=(const ContentPanel&) = delete;

    void onImGuiRender(bool playMode);
    void stopAudioPreview();
    void setSelectionHandler(std::function<void(std::string)> handler) { m_select = std::move(handler); }
    void setSelectedAsset(std::string path);
    void revealReferences(std::initializer_list<std::string_view> paths);
    [[nodiscard]] std::uintptr_t luaIcon() const noexcept { return m_luaIcon; }

private:
    void syncToProject();
    void drawAudioControls();
    void drawBreadcrumb();
    void drawFolderList();
    void navigateInto(const std::filesystem::path& folderName);
    void navigateUp();

    // Non-owning; lifetime must outlive this panel.
    Project* m_project = nullptr;
    Renderer* m_renderer = nullptr;
    std::filesystem::path m_trackedRoot;
    std::filesystem::path m_relative; // path under project root; empty = root
    std::uintptr_t m_folderIcon = 0;
    std::uintptr_t m_returnIcon = 0;
    std::uintptr_t m_luaIcon = 0;
    std::function<void(std::string)> m_select;
    std::string m_selectedAsset;
    std::vector<std::string> m_revealedReferences;
    bool m_scrollToReference = false;
    Scene m_previewScene;
    AudioSystem m_preview{m_previewScene};
    EntityId m_previewEntity = kInvalidEntityId;
    AssetImportSettings m_audioImport;
    std::string m_audioError;
    float m_zoom = 0.0f; // 0 = list, 1 = large icons
};

} // namespace sat
