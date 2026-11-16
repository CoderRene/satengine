#pragma once

#include "AssetImportSettings.hpp"
#include "AudioSystem.hpp"
#include "Window.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <initializer_list>
#include <span>
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

    void onImGuiRender(bool playMode, std::span<const FileDrop> fileDrops = {}, bool canMoveFiles = true);
    void stopAudioPreview();
    [[nodiscard]] bool deletingFiles() const { return m_delete.valid(); }
    void setSelectionHandler(std::function<void(std::string)> handler) { m_select = std::move(handler); }
    void setMoveHandler(std::function<std::string(std::string_view, std::string_view)> handler) {
        m_onMove = std::move(handler);
    }
    void setSelectedAsset(std::string path);
    void revealReferences(std::initializer_list<std::string_view> paths);
    [[nodiscard]] std::uintptr_t luaIcon() const noexcept { return m_luaIcon; }

private:
    void syncToProject();
    void drawAudioControls();
    void drawBreadcrumb();
    void drawFolderList(bool canMoveFiles, bool canReferenceAssets);
    bool moveFile(std::string_view relativePath, const std::filesystem::path& targetFolder);
    void moveFiles(std::string_view paths, const std::filesystem::path& targetFolder);
    struct DeleteResult {
        std::vector<std::string> deleted;
        std::string error;
    };
    void deletePendingFiles();
    void finishDelete();
    void drawNewFolderDialog(bool allowCreate, bool requested);
    void acceptFileDrops(std::span<const FileDrop> fileDrops);
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
    std::function<std::string(std::string_view, std::string_view)> m_onMove;
    std::string m_selectedAsset;
    std::filesystem::path m_selectionFolder;
    std::vector<std::string> m_selectedFiles;
    std::vector<std::string> m_pendingDelete;
    std::string m_selectionAnchor;
    std::vector<std::string> m_revealedReferences;
    bool m_scrollToReference = false;
    Scene m_previewScene;
    AudioSystem m_preview{m_previewScene};
    EntityId m_previewEntity = kInvalidEntityId;
    AssetImportSettings m_audioImport;
    std::string m_audioError;
    std::string m_dropStatus;
    char m_newFolderName[256]{};
    std::string m_newFolderError;
    double m_parentDragHoverStart = 0.0;
    int m_parentDragHoverFrame = -1;
    bool m_parentDragNavigated = false;
    float m_zoom = 0.0f; // 0 = list, 1 = large icons
    std::filesystem::path m_deleteRoot;
    std::future<DeleteResult> m_delete; // Joins on panel teardown.
};

} // namespace sat
