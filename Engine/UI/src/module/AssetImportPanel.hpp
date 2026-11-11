#pragma once

#include "AssetImportSettings.hpp"

#include <functional>
#include <string>

namespace sat {
class Project;
class Renderer;
// Window-thread only. Project/renderer references must outlive this panel.
// Owns the single asset selection, saved settings, draft and deferred UI action.
class AssetImportPanel {
public:
    AssetImportPanel(Project& project, Renderer& renderer);
    void select(std::string path);
    void clear();
    [[nodiscard]] bool active() const { return !m_path.empty(); }
    [[nodiscard]] bool dirty() const { return m_edited != m_saved; }
    [[nodiscard]] const std::string& path() const { return m_path; }
    void request(std::function<void()> action);
    void draw(bool readOnly);
    void drawPrompt();
private:
    [[nodiscard]] bool apply();
    void revert();
    Project& m_project;
    Renderer& m_renderer;
    std::string m_path;
    std::string m_error;
    std::string m_info;
    AssetImportSettings m_saved;
    AssetImportSettings m_edited;
    bool m_valid = true;
    bool m_openPrompt = false;
    std::function<void()> m_pending;
};
}
