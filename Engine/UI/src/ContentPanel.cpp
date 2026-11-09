#include "ContentPanel.hpp"

#include "EnginePaths.hpp"
#include "Project.hpp"
#include "Renderer.hpp"

#include <imgui.h>

#include <stb_image.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace sat {
namespace fs = std::filesystem;

namespace {

struct Entry {
    fs::path name;
    bool isDirectory = false;
};

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

std::uintptr_t loadIconTexture(Renderer& renderer, const char* assetRelativePath) {
    const std::string absPath = std::string(SAT_ENGINE_ASSETS_DIR) + "/" + assetRelativePath;
    const char* paths[] = {absPath.c_str(), assetRelativePath};

    for (const char* path : paths) {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load(path, &width, &height, &channels, 4);
        if (pixels == nullptr) {
            continue;
        }

        const std::uintptr_t texture = renderer.createTexture(pixels, width, height);
        stbi_image_free(pixels);
        if (texture != 0) {
            return texture;
        }
    }

    std::cerr << "Failed to load icon: " << assetRelativePath << '\n';
    return 0;
}

void destroyIconTexture(Renderer& renderer, std::uintptr_t& texture) {
    if (texture != 0) {
        renderer.destroyTexture(texture);
        texture = 0;
    }
}

} // namespace

ContentPanel::ContentPanel(Project& project, Renderer& renderer)
    : m_project(&project)
    , m_renderer(&renderer) {
    m_folderIcon = loadIconTexture(renderer, "Icons/folder_ic.png");
    m_returnIcon = loadIconTexture(renderer, "Icons/return_ic.png");
}

ContentPanel::~ContentPanel() {
    destroyIconTexture(*m_renderer, m_folderIcon);
    destroyIconTexture(*m_renderer, m_returnIcon);
}

void ContentPanel::syncToProject() {
    if (!m_project->isOpen()) {
        m_trackedRoot.clear();
        m_relative.clear();
        return;
    }

    if (m_trackedRoot != m_project->root()) {
        m_trackedRoot = m_project->root();
        m_relative.clear();
    }
}

void ContentPanel::navigateInto(const fs::path& folderName) {
    m_relative /= folderName;
}

void ContentPanel::navigateUp() {
    if (m_relative.empty()) {
        return;
    }
    m_relative = m_relative.parent_path();
}

void ContentPanel::onImGuiRender() {
    syncToProject();

    ImGui::Begin("Content");
    if (m_project->isOpen()) {
        drawBreadcrumb();
        ImGui::Separator();
        drawFolderList();
    } else {
        ImGui::TextUnformatted("Create or open a project from File to browse files.");
    }
    ImGui::End();
}

void ContentPanel::drawBreadcrumb() {
    if (m_relative.empty()) {
        ImGui::TextDisabled("%s", m_project->root().string().c_str());
    } else {
        ImGui::TextDisabled("%s", (m_project->root() / m_relative).string().c_str());
    }

    const float sliderWidth = 120.0f;
    const float sliderX =
        (std::max)(ImGui::GetCursorPosX() + 8.0f,
                   ImGui::GetWindowContentRegionMax().x - sliderWidth);
    ImGui::SameLine(sliderX);
    ImGui::SetNextItemWidth(sliderWidth);
    ImGui::SliderFloat("##zoom", &m_zoom, 0.0f, 1.0f, "");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("View size (list <-> large icons)");
    }
}

void ContentPanel::drawFolderList() {
    const fs::path current = m_project->root() / m_relative;

    std::error_code ec;
    if (!fs::is_directory(current, ec)) {
        ImGui::TextUnformatted("Folder is missing. Returning to project root.");
        m_relative.clear();
        return;
    }

    std::vector<Entry> entries;
    if (!m_relative.empty()) {
        entries.push_back({fs::path(".."), true});
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(current, ec)) {
        if (ec) {
            break;
        }
        std::error_code typeEc;
        const bool isDir = entry.is_directory(typeEc);
        if (typeEc) {
            continue;
        }
        entries.push_back({entry.path().filename(), isDir});
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.name == "..") {
            return true;
        }
        if (b.name == "..") {
            return false;
        }
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory && !b.isDirectory;
        }
        return a.name.string() < b.name.string();
    });

    constexpr float kListThreshold = 0.15f;
    const bool listView = m_zoom <= kListThreshold;
    const float iconSize = listView
        ? ImGui::GetTextLineHeight()
        : lerp(40.0f, 96.0f, (m_zoom - kListThreshold) / (1.0f - kListThreshold));

    const ImTextureRef folderIcon((ImTextureID)(std::uintptr_t)m_folderIcon);
    const ImTextureRef returnIcon((ImTextureID)(std::uintptr_t)m_returnIcon);

    if (listView) {
        for (const Entry& entry : entries) {
            const std::string name = entry.name.string();
            const bool isParent = name == "..";
            ImGui::PushID(name.c_str());

            if (isParent && m_returnIcon != 0) {
                ImGui::Image(returnIcon, ImVec2(iconSize, iconSize));
                ImGui::SameLine();
            } else if (entry.isDirectory && m_folderIcon != 0) {
                ImGui::Image(folderIcon, ImVec2(iconSize, iconSize));
                ImGui::SameLine();
            }

            if (ImGui::Selectable(name.c_str())) {
                if (isParent) {
                    navigateUp();
                }
            }
            if (entry.isDirectory && !isParent &&
                ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                navigateInto(entry.name);
            }

            ImGui::PopID();
        }
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float textH = ImGui::GetTextLineHeight();
    const float cellW = iconSize + style.ItemSpacing.x;
    const float cellH = iconSize + textH + style.ItemSpacing.y * 2.0f;
    const float availX = ImGui::GetContentRegionAvail().x;
    const int columns = (std::max)(1, static_cast<int>(availX / cellW));
    int column = 0;

    for (const Entry& entry : entries) {
        const std::string name = entry.name.string();
        ImGui::PushID(name.c_str());

        if (column > 0) {
            ImGui::SameLine(0.0f, style.ItemSpacing.x);
        }

        ImGui::BeginGroup();
        const bool pressed = ImGui::InvisibleButton("##cell", ImVec2(iconSize, cellH));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 min = ImGui::GetItemRectMin();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        if (hovered) {
            draw->AddRectFilled(
                min,
                ImVec2(min.x + iconSize, min.y + cellH),
                ImGui::GetColorU32(ImGuiCol_HeaderHovered),
                style.FrameRounding);
        }

        const ImVec2 iconMin(min.x, min.y);
        const ImVec2 iconMax(min.x + iconSize, min.y + iconSize);
        const bool isParent = name == "..";
        if (isParent && m_returnIcon != 0) {
            draw->AddImage(returnIcon, iconMin, iconMax);
        } else if (entry.isDirectory && m_folderIcon != 0) {
            draw->AddImage(folderIcon, iconMin, iconMax);
        } else {
            draw->AddRectFilled(
                iconMin, iconMax, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
            const char initial = name.empty() ? '?' : name[0];
            const ImVec2 glyphSize = ImGui::CalcTextSize(&initial, &initial + 1);
            draw->AddText(
                ImVec2(
                    iconMin.x + (iconSize - glyphSize.x) * 0.5f,
                    iconMin.y + (iconSize - glyphSize.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text),
                &initial,
                &initial + 1);
        }

        std::string label = name;
        if (ImGui::CalcTextSize(label.c_str()).x > iconSize) {
            while (label.size() > 1 &&
                   ImGui::CalcTextSize((label + "..").c_str()).x > iconSize) {
                label.pop_back();
            }
            label += "..";
        }
        const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
        draw->AddText(
            ImVec2(
                min.x + (iconSize - labelSize.x) * 0.5f,
                min.y + iconSize + style.ItemSpacing.y),
            ImGui::GetColorU32(ImGuiCol_Text),
            label.c_str());

        if (entry.isDirectory) {
            if (isParent && pressed) {
                navigateUp();
            } else if (!isParent && hovered &&
                       ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                navigateInto(entry.name);
            }
        }

        ImGui::EndGroup();
        ImGui::PopID();

        column = (column + 1) % columns;
    }
}

} // namespace sat
