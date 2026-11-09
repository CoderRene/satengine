#pragma once

#include <cstdint>
#include <filesystem>

namespace sat {

class Project;
class Renderer;

class ContentPanel {
public:
    ContentPanel(Project& project, Renderer& renderer);
    ~ContentPanel();

    ContentPanel(const ContentPanel&) = delete;
    ContentPanel& operator=(const ContentPanel&) = delete;

    void onImGuiRender();

private:
    void syncToProject();
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
    float m_zoom = 0.0f; // 0 = list, 1 = large icons
};

} // namespace sat
