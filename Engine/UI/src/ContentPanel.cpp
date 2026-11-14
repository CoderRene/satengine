#include "ContentPanel.hpp"

#include "AssetData.hpp"
#include "EnginePaths.hpp"
#include "Project.hpp"
#include "Renderer.hpp"
#include "OpenLuaFile.hpp"

#include <imgui.h>

#include <stb_image.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
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

bool isLuaFile(const fs::path& name) {
    return name.extension() == ".lua";
}

bool isToolingEntry(const fs::path& name) {
    const std::string fileName = name.filename().string();
    return fileName == "LuaDefs" || fileName == ".satengine" || fileName == ".vscode" ||
           fileName == ".luarc.json" || fileName == "SatEngine.code-workspace" ||
           fileName == "selene.toml" || fileName == "satengine.yml";
}

void openLuaInCursor(Project& project, const fs::path& relativeDir, const fs::path& fileName) {
    const fs::path absolute = project.root() / relativeDir / fileName;
    const bool opened = openInCursor(project.root(), absolute);
    (void)opened; // Failure is logged inside openInCursor.
}

} // namespace

ContentPanel::ContentPanel(Project& project, Renderer& renderer)
    : m_project(&project)
    , m_renderer(&renderer) {
    m_folderIcon = loadIconTexture(renderer, "Icons/folder_ic.png");
    m_returnIcon = loadIconTexture(renderer, "Icons/return_ic.png");
    m_luaIcon = loadIconTexture(renderer, "Icons/lua_ic.png");
}

ContentPanel::~ContentPanel() {
    destroyIconTexture(*m_renderer, m_folderIcon);
    destroyIconTexture(*m_renderer, m_returnIcon);
    destroyIconTexture(*m_renderer, m_luaIcon);
}

void ContentPanel::syncToProject() {
    if (!m_project->isOpen()) {
        setSelectedAsset({});
        m_trackedRoot.clear();
        m_relative.clear();
        return;
    }

    if (m_trackedRoot != m_project->root()) {
        setSelectedAsset({});
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

void ContentPanel::stopAudioPreview() {
    m_preview.stop();
}

void ContentPanel::setSelectedAsset(std::string path) {
    m_revealedReferences.clear();
    m_scrollToReference = false;
    if (path == m_selectedAsset) { return; }
    stopAudioPreview();
    m_selectedAsset = std::move(path);
    m_audioError.clear();
    m_audioImport = {};
    if (isAudioAssetPath(m_selectedAsset)) {
        const auto source = m_project->root() / std::filesystem::path(
            std::u8string(m_selectedAsset.begin(), m_selectedAsset.end()));
        if (!loadImportSettings(source, m_audioImport, m_audioError)) {
            m_audioError = "Cannot read audio import settings: " + m_audioError;
        }
    }
}

void ContentPanel::revealReferences(std::initializer_list<std::string_view> paths) {
    syncToProject();
    setSelectedAsset({});
    if (!m_project->isOpen()) { return; }

    constexpr std::string_view kScriptsPrefix = "Scripts/";
    for (const std::string_view path : paths) {
        if (!isValidAssetPath(path) &&
            !(path.starts_with(kScriptsPrefix) && isValidScriptPath(path.substr(kScriptsPrefix.size())))) {
            continue;
        }
        const fs::path relative{std::u8string(path.begin(), path.end())};
        std::error_code error;
        if (!fs::is_regular_file(m_project->root() / relative, error)) { continue; }

        if (m_revealedReferences.empty()) {
            m_relative = relative.parent_path();
            m_scrollToReference = true;
        }
        m_revealedReferences.emplace_back(path);
    }
}

void ContentPanel::drawAudioControls() {
    if (!isAudioAssetPath(m_selectedAsset)) { return; }
    ImGui::TextUnformatted(m_selectedAsset.c_str());
    if (ImGui::Button("Preview Audio")) {
        m_preview.setAssetReader(fileAssetReader(m_project->root()));
        m_previewScene.clear();
        auto& entity = m_previewScene.createEntity("Preview");
        m_previewEntity = entity.id();
        entity.hasAudioSource = true;
        entity.audioSource.clipPath = m_selectedAsset;
        entity.audioSource.spatial = false;
        entity.audioSource.volume = 0.5f;
        m_audioError.clear();
        if (!m_preview.start()) { m_audioError = m_preview.error(); }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_preview.running());
    if (ImGui::Button("Stop Preview")) { stopAudioPreview(); }
    ImGui::EndDisabled();

    int mode = static_cast<int>(m_audioImport.audioLoadMode);
    if (ImGui::Combo("Audio Import", &mode, "Auto\0Decode\0Stream\0")) {
        auto settings = m_audioImport;
        settings.audioLoadMode = static_cast<AudioLoadMode>(mode);
        const auto source = m_project->root() / std::filesystem::path(
            std::u8string(m_selectedAsset.begin(), m_selectedAsset.end()));
        if (saveImportSettings(source, settings, m_audioError)) {
            m_audioImport = settings;
            stopAudioPreview();
        }
    }
    if (!m_audioError.empty()) { ImGui::TextWrapped("%s", m_audioError.c_str()); }
}

void ContentPanel::onImGuiRender(bool playMode) {
    syncToProject();
    if (m_preview.running() && (playMode || !m_preview.isPlaying(m_previewEntity))) {
        stopAudioPreview();
    }

    if (!ImGui::Begin("Content")) {
        ImGui::End();
        return;
    }
    if (m_project->isOpen()) {
        drawBreadcrumb();
        ImGui::Separator();
        ImGui::BeginDisabled(playMode);
        drawAudioControls();
        ImGui::EndDisabled();
        drawFolderList();
        if (ImGui::BeginPopupContextWindow("ContentContext",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            const bool createMaterial = ImGui::MenuItem("Create Material", nullptr, false, !playMode);
            const bool createShader = ImGui::MenuItem("Create Slang Shader", nullptr, false, !playMode);
            if (createMaterial || createShader) {
                constexpr std::string_view kShaderTemplate = R"(import Material;
import MeshFragment;

[shader("fragment")]
float4 psMain(MeshFragment input) : SV_Target
{
    float4 base = sampleBaseColor(input.uv);
    return float4(base.rgb, materialOpacity(base.a));
}
)";
                const std::string stem = createShader ? "Shader" : "Material";
                const char* extension = createShader ? ".slang" : ".satmat";
                std::error_code ec;
                const auto relative = m_relative.generic_u8string();
                const auto folder = relative == u8"Assets" || relative.starts_with(u8"Assets/")
                    ? m_relative : fs::path("Assets");
                fs::create_directories(m_project->root() / folder, ec);
                for (int suffix = 0; !ec && suffix < 10000; ++suffix) {
                    const auto name = suffix == 0 ? stem + extension
                        : stem + " " + std::to_string(suffix) + extension;
                    const auto path = m_project->root() / folder / name;
                    if (fs::exists(path, ec)) { continue; }
                    if (ec) { break; }
                    std::ofstream file(path, std::ios::binary);
                    if (createShader) { file << kShaderTemplate; }
                    else { file << encodeMaterial(MaterialData{}); }
                    file.close();
                    if (!file) { ec = std::make_error_code(std::errc::io_error); break; }
                    m_relative = folder;
                    const auto encoded = (folder / name).generic_u8string();
                    if (createMaterial && m_select) { m_select(std::string(encoded.begin(), encoded.end())); }
                    break;
                }
                if (ec) { std::cerr << "Cannot create " << stem << ": " << ec.message() << '\n'; }
            }
            ImGui::EndPopup();
        }
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
    // Navigation can change m_relative while the current folder's rows are still drawing.
    const fs::path listedFolder = m_relative;
    const fs::path current = m_project->root() / listedFolder;

    std::error_code ec;
    if (!fs::is_directory(current, ec)) {
        ImGui::TextUnformatted("Folder is missing. Returning to project root.");
        m_relative.clear();
        return;
    }

    std::vector<Entry> entries;
    if (!listedFolder.empty()) {
        entries.push_back({fs::path(".."), true});
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(current, ec)) {
        if (ec) {
            break;
        }
        const fs::path fileName = entry.path().filename();
        if (isToolingEntry(fileName) || fileName.extension() == ".satimport" || fileName.string().ends_with(".satimport.tmp") || fileName.string().ends_with(".satmat.tmp")) {
            continue;
        }
        std::error_code typeEc;
        const bool isDir = entry.is_directory(typeEc);
        if (typeEc) {
            continue;
        }
        entries.push_back({fileName, isDir});
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
    const ImTextureRef luaIcon(static_cast<ImTextureID>(m_luaIcon));

    const auto isHighlighted = [this](std::string_view path, bool directory) {
        if (!directory && path == m_selectedAsset) { return true; }
        return std::any_of(m_revealedReferences.begin(), m_revealedReferences.end(),
            [path, directory](std::string_view reference) {
                return directory ? reference.starts_with(path) && reference.size() > path.size() &&
                    reference[path.size()] == '/' : reference == path;
            });
    };
    const auto scrollToReference = [this](std::string_view path) {
        if (m_scrollToReference && !m_revealedReferences.empty() && path == m_revealedReferences.front()) {
            ImGui::SetScrollHereY();
            m_scrollToReference = false;
        }
    };

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
            } else if (!entry.isDirectory && m_luaIcon != 0 && isLuaFile(entry.name)) {
                ImGui::Image(luaIcon, ImVec2(iconSize, iconSize));
                ImGui::SameLine();
            }

            const auto encoded = (listedFolder / entry.name).generic_u8string();
            const std::string assetPath(encoded.begin(), encoded.end());
            if (ImGui::Selectable(name.c_str(), isHighlighted(assetPath, entry.isDirectory))) {
                if (!entry.isDirectory && m_select) { m_select(assetPath); }
                if (isParent) {
                    navigateUp();
                }
            }
            scrollToReference(assetPath);
            if (entry.isDirectory && !isParent &&
                ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                navigateInto(entry.name);
            }
            if (!entry.isDirectory && isLuaFile(entry.name) &&
                ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                openLuaInCursor(*m_project, listedFolder, entry.name);
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
        const bool visible = ImGui::IsItemVisible();
        const ImVec2 min = ImGui::GetItemRectMin();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        const auto encoded = (listedFolder / entry.name).generic_u8string();
        const std::string assetPath(encoded.begin(), encoded.end());
        if (pressed && !entry.isDirectory && m_select) { m_select(assetPath); }
        if (hovered || isHighlighted(assetPath, entry.isDirectory)) {
            draw->AddRectFilled(
                min,
                ImVec2(min.x + iconSize, min.y + cellH),
                ImGui::GetColorU32(ImGuiCol_HeaderHovered),
                style.FrameRounding);
        }

        const ImVec2 iconMin(min.x, min.y);
        const ImVec2 iconMax(min.x + iconSize, min.y + iconSize);
        const bool isParent = name == "..";
        const auto thumbnail = visible && !entry.isDirectory && isMaterialAssetPath(assetPath)
            ? m_renderer->materialThumbnail(assetPath) : 0;
        if (thumbnail != 0) {
            draw->AddImage(ImTextureRef(static_cast<ImTextureID>(thumbnail)), iconMin, iconMax);
        } else if (isParent && m_returnIcon != 0) {
            draw->AddImage(returnIcon, iconMin, iconMax);
        } else if (entry.isDirectory && m_folderIcon != 0) {
            draw->AddImage(folderIcon, iconMin, iconMax);
        } else if (!entry.isDirectory && m_luaIcon != 0 && isLuaFile(entry.name)) {
            draw->AddImage(luaIcon, iconMin, iconMax);
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
        } else if (isLuaFile(entry.name) && hovered &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            openLuaInCursor(*m_project, listedFolder, entry.name);
        }

        ImGui::EndGroup();
        scrollToReference(assetPath);
        ImGui::PopID();

        column = (column + 1) % columns;
    }
}

} // namespace sat
