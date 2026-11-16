#include "ContentPanel.hpp"

#include "AssetData.hpp"
#include "module/ContentDragDrop.hpp"
#include "EnginePaths.hpp"
#include "Project.hpp"
#include "Renderer.hpp"
#include "OpenLuaFile.hpp"
#include "Window.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <stb_image.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace sat {
namespace fs = std::filesystem;

namespace {

struct Entry {
    fs::path name;
    bool isDirectory = false;
};

std::string pathText(const fs::path& path) {
    const auto encoded = path.u8string();
    return {encoded.begin(), encoded.end()};
}

bool isWithin(const fs::path& path, const fs::path& root) {
    return std::mismatch(root.begin(), root.end(), path.begin(), path.end()).first == root.end();
}

bool destinationAvailable(const fs::path& path, std::error_code& error) {
    const auto status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory) { error.clear(); }
    if (!error && fs::exists(status)) { error = std::make_error_code(std::errc::file_exists); }
    return !error;
}

void copyDroppedPath(const fs::path& source, const fs::path& folder, std::error_code& error) {
    const auto status = fs::symlink_status(source, error);
    if (error) { return; }

    const auto destination = folder / source.filename();
    if (!destinationAvailable(destination, error)) { return; }
    if (fs::is_regular_file(status)) {
        fs::copy_file(source, destination, fs::copy_options::none, error);
        return;
    }
    if (!fs::is_directory(status) || source.filename().empty()) {
        error = std::make_error_code(std::errc::operation_not_supported);
        return;
    }

    const auto sourceRoot = fs::canonical(source, error);
    if (error) { return; }
    if (isWithin(folder, sourceRoot)) {
        error = std::make_error_code(std::errc::invalid_argument);
        return;
    }

    // Claim a new top-level directory so the recursive copy cannot merge with an existing folder.
    if (!fs::create_directory(destination, error)) {
        if (!error) { error = std::make_error_code(std::errc::file_exists); }
        return;
    }
    fs::copy(source, destination, fs::copy_options::recursive | fs::copy_options::skip_symlinks, error);
    if (error) {
        std::error_code cleanupError;
        fs::remove_all(destination, cleanupError);
        if (cleanupError) {
            std::cerr << "Cannot remove incomplete copy " << pathText(destination) << ": "
                      << cleanupError.message() << '\n';
        }
    }
}

bool isProtectedFile(std::string_view path) {
    const auto equals = [path](std::string_view name) {
        return std::ranges::equal(path, name, [](char a, char b) {
            const auto lower = [](char ch) { return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch; };
            return lower(a) == lower(b);
        });
    };
    return equals("SatEngineProject.sat") || equals("Scene.sat");
}

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

bool isToolingEntry(std::string_view fileName) {
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
        m_pendingDelete.clear();
        m_selectedFiles.clear();
        m_selectionAnchor.clear();
        m_dropStatus.clear();
        m_relative.clear();
        return;
    }

    if (m_trackedRoot != m_project->root()) {
        setSelectedAsset({});
        m_trackedRoot = m_project->root();
        m_pendingDelete.clear();
        m_selectedFiles.clear();
        m_selectionAnchor.clear();
        m_dropStatus.clear();
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

void ContentPanel::acceptFileDrops(std::span<const FileDrop> fileDrops) {
    if (fileDrops.empty() || ImGui::GetTopMostPopupModal() != nullptr) { return; }
    auto* panel = ImGui::GetCurrentWindow();
    if (!ImGui::IsWindowContentHoverable(panel)) { return; }

    fs::path destination;
    std::size_t failed = 0;
    std::string firstError;
    for (const auto& drop : fileDrops) {
        const ImVec2 position(static_cast<float>(drop.position.x), static_cast<float>(drop.position.y));
        if (!panel->OuterRectClipped.Contains(position)) { continue; }
        ImGuiWindow* hovered = nullptr;
        ImGui::FindHoveredWindowEx(position, true, &hovered, nullptr);
        if (hovered != panel) { continue; }

        if (destination.empty()) {
            std::error_code error;
            const auto root = fs::canonical(m_project->root(), error);
            if (!error) { destination = fs::canonical(root / m_relative, error); }
            if (error || !fs::is_directory(destination, error) || !isWithin(destination, root)) {
                m_dropStatus = "Cannot copy files into this folder. Open a folder inside the project.";
                if (error) { m_dropStatus += " " + error.message(); }
                return;
            }
        }

        for (const auto& source : drop.paths) {
            std::error_code error;
            copyDroppedPath(source, destination, error);
            if (error) {
                ++failed;
                if (firstError.empty()) {
                    firstError = pathText(source.filename()) + ": " + error.message();
                }
            }
        }
    }

    if (!destination.empty()) {
        m_dropStatus = failed == 0 ? std::string{} :
            "Failed to copy " + std::to_string(failed) + ". " + firstError;
    }
}

bool ContentPanel::moveFile(std::string_view relativePath, const fs::path& targetFolder) {
    const fs::path relative(std::u8string(relativePath.begin(), relativePath.end()));
    std::error_code error;
    const auto root = fs::canonical(m_project->root(), error);
    const auto sourceFolder = error ? fs::path{} : fs::canonical(root / relative.parent_path(), error);
    const auto folder = error ? fs::path{} : fs::canonical(root / targetFolder, error);
    if (error || !isWithin(sourceFolder, root) || !isWithin(folder, root) || !fs::is_directory(folder, error)) {
        m_dropStatus = "Choose a destination folder inside the project.";
        return false;
    }
    if (sourceFolder == folder) { return false; }

    const auto encoded = (targetFolder / relative.filename()).generic_u8string();
    const std::string newPath(encoded.begin(), encoded.end());
    if (isValidAssetPath(relativePath) && !isValidAssetPath(newPath)) {
        m_dropStatus = "Move assets to a folder inside Assets.";
        return false;
    }
    if ((isUiDocumentPath(relativePath) && !isUiDocumentPath(newPath)) ||
        (isUiFontPath(relativePath) && !isUiFontPath(newPath))) {
        m_dropStatus = "Move UI assets to a folder inside Assets/UI.";
        return false;
    }
    if (relativePath.starts_with("Scripts/") && relative.extension() == ".lua" &&
        (!newPath.starts_with("Scripts/") || !isValidScriptPath(std::string_view(newPath).substr(8)))) {
        m_dropStatus = "Move scripts to a folder inside Scripts.";
        return false;
    }

    const auto source = sourceFolder / relative.filename();
    const auto destination = folder / relative.filename();
    const auto sourceStatus = fs::symlink_status(source, error);
    if (error || !fs::is_regular_file(sourceStatus)) {
        m_dropStatus = "Only regular files can be moved.";
        return false;
    }

    auto sourceSettings = source;
    sourceSettings += ".satimport";
    auto destinationSettings = destination;
    destinationSettings += ".satimport";
    const auto settingsStatus = fs::symlink_status(sourceSettings, error);
    if (error == std::errc::no_such_file_or_directory) { error.clear(); }
    const bool hasSettings = fs::exists(settingsStatus);
    if (!error && hasSettings && !fs::is_regular_file(settingsStatus)) {
        error = std::make_error_code(std::errc::operation_not_supported);
    }

    if (!error && destinationAvailable(destination, error) &&
        destinationAvailable(destinationSettings, error)) {
        if (m_selectedAsset == relativePath) { stopAudioPreview(); }
        if (hasSettings) { fs::rename(sourceSettings, destinationSettings, error); }
        if (!error) {
            fs::rename(source, destination, error);
            if (error && hasSettings) {
                std::error_code rollbackError;
                fs::rename(destinationSettings, sourceSettings, rollbackError);
                if (rollbackError) {
                    m_dropStatus = "Could not move " + pathText(relative.filename()) +
                        ". Import settings could not be restored: " + rollbackError.message();
                    return false;
                }
            }
        }
    }
    if (error) {
        m_dropStatus = "Could not move " + pathText(relative.filename()) + ": " + error.message();
        return false;
    }

    if (m_onMove) { m_dropStatus = m_onMove(relativePath, newPath); }
    if (m_selectedAsset == relativePath) {
        setSelectedAsset({});
        if (m_select) { m_select({}); }
    }
    return true;
}

void ContentPanel::moveFiles(std::string_view paths, const fs::path& targetFolder) {
    std::string firstError;
    bool moved = false;
    while (!paths.empty()) {
        const auto end = paths.find('\0');
        if (end == std::string_view::npos) { break; }
        const auto source = paths.substr(0, end);
        m_dropStatus.clear();
        if (moveFile(source, targetFolder)) {
            moved = true;
            std::erase_if(m_selectedFiles, [source](const std::string& path) { return path == source; });
            if (m_selectionAnchor == source) { m_selectionAnchor.clear(); }
        }
        if (firstError.empty()) { firstError = std::move(m_dropStatus); }
        paths.remove_prefix(end + 1);
    }
    m_dropStatus = std::move(firstError);
    if (moved) { m_renderer->setProjectAssetRoot(m_project->root()); }
}

void ContentPanel::deletePendingFiles() {
    m_dropStatus.clear();
    if (std::ranges::find(m_pendingDelete, m_selectedAsset) != m_pendingDelete.end()) {
        stopAudioPreview();
    }
    try {
        m_deleteRoot = m_trackedRoot;
        m_delete = std::async(std::launch::async, [rootPath = m_deleteRoot, paths = std::move(m_pendingDelete)]() mutable {
            DeleteResult result;
            result.deleted.reserve(paths.size());
            std::error_code rootError;
            const auto root = fs::canonical(rootPath, rootError);
            for (auto& path : paths) {
                const fs::path relative(std::u8string(path.begin(), path.end()));
                auto error = rootError;
                const auto folder = error ? fs::path{} : fs::canonical(root / relative.parent_path(), error);
                if (!error && (!isWithin(folder, root) ||
                    (folder == root && isProtectedFile(pathText(relative.filename()))))) {
                    error = std::make_error_code(std::errc::operation_not_permitted);
                }
                const auto file = folder / relative.filename();
                if (!error) {
                    const auto status = fs::symlink_status(file, error);
                    if (!error && !fs::is_regular_file(status)) {
                        error = std::make_error_code(std::errc::operation_not_supported);
                    }
                }
                if (!error && !fs::remove(file, error) && !error) {
                    error = std::make_error_code(std::errc::no_such_file_or_directory);
                }
                if (error) {
                    if (result.error.empty()) { result.error = "Could not delete " + path + ": " + error.message(); }
                    continue;
                }

                result.deleted.push_back(std::move(path));
                auto sidecar = file;
                sidecar += ".satimport";
                const auto status = fs::symlink_status(sidecar, error);
                if (error == std::errc::no_such_file_or_directory) { error.clear(); }
                if (!error && fs::is_regular_file(status)) { fs::remove(sidecar, error); }
                if (error && result.error.empty()) {
                    result.error = "Could not delete import settings for " + result.deleted.back() + ": " + error.message();
                }
            }
            return result;
        });
    } catch (const std::exception& error) {
        m_dropStatus = "Could not start deletion: " + std::string(error.what());
    }
}

void ContentPanel::finishDelete() {
    using namespace std::chrono_literals;
    if (!m_delete.valid() || m_delete.wait_for(0s) != std::future_status::ready) { return; }
    const bool currentProject = m_deleteRoot == m_trackedRoot;
    try {
        auto result = m_delete.get();
        if (!currentProject) { return; }
        m_dropStatus = std::move(result.error);
        bool clearedInspector = false;
        for (const auto& path : result.deleted) {
            m_renderer->invalidateAsset(path);
            std::erase(m_selectedFiles, path);
            if (m_selectionAnchor == path) { m_selectionAnchor.clear(); }
            if (m_selectedAsset == path) {
                setSelectedAsset({});
                clearedInspector = true;
            }
        }
        if (clearedInspector && m_select) { m_select({}); }
    } catch (const std::exception& error) {
        if (currentProject) { m_dropStatus = "Could not finish deletion: " + std::string(error.what()); }
    }
}

void ContentPanel::drawNewFolderDialog(bool allowCreate, bool requested) {
    constexpr char kPopup[] = "New Folder";
    if (requested) {
        m_newFolderName[0] = '\0';
        m_newFolderError.clear();
        ImGui::OpenPopup(kPopup);
    }
    if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { return; }

    if (ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); }
    ImGui::BeginDisabled(!allowCreate);
    const bool submitted = ImGui::InputText("Name", m_newFolderName, sizeof(m_newFolderName),
        ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("Create") || submitted) {
        const std::string_view name(m_newFolderName);
        if (name.empty() || name.find_first_of("<>:\"/\\|?*") != std::string_view::npos ||
            name.back() == '.' || name.back() == ' ' || isToolingEntry(name) ||
            name.ends_with(".satimport") || name.ends_with(".satimport.tmp") || name.ends_with(".satmat.tmp") ||
            std::any_of(name.begin(), name.end(), [](unsigned char ch) { return ch < 32; })) {
            m_newFolderError = "Enter a valid folder name.";
        } else {
            std::error_code error;
            const auto root = fs::canonical(m_project->root(), error);
            const auto parent = error ? fs::path{} : fs::canonical(root / m_relative, error);
            if (error || !isWithin(parent, root)) {
                m_newFolderError = "Choose a directory inside the project.";
            } else {
                const fs::path folderName(std::u8string(name.begin(), name.end()));
                if (fs::create_directory(parent / folderName, error)) {
                    m_dropStatus.clear();
                    ImGui::CloseCurrentPopup();
                } else {
                    m_newFolderError = error ? "Could not create folder: " + error.message() :
                        "A folder with that name already exists.";
                }
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { ImGui::CloseCurrentPopup(); }
    if (!m_newFolderError.empty()) { ImGui::TextWrapped("%s", m_newFolderError.c_str()); }
    ImGui::EndPopup();
}

void ContentPanel::onImGuiRender(bool playMode, std::span<const FileDrop> fileDrops, bool canMoveFiles) {
    syncToProject();
    finishDelete();
    if (m_preview.running() && (playMode || !m_preview.isPlaying(m_previewEntity))) {
        stopAudioPreview();
    }

    if (!ImGui::Begin("Content")) {
        ImGui::End();
        return;
    }
    if (m_project->isOpen()) {
        if (!playMode) { acceptFileDrops(fileDrops); }
        if (m_delete.valid() && m_deleteRoot == m_trackedRoot) { ImGui::TextUnformatted("Deleting files…"); }
        drawBreadcrumb();
        ImGui::Separator();
        if (!m_dropStatus.empty()) { ImGui::TextWrapped("%s", m_dropStatus.c_str()); }
        ImGui::BeginDisabled(playMode);
        drawAudioControls();
        ImGui::EndDisabled();
        const bool allowMove = !playMode && canMoveFiles && !m_delete.valid();
        const auto* activePayload = ImGui::GetDragDropPayload();
        if (allowMove && activePayload && activePayload->IsDataType(kContentFilePayload)) {
            ImGui::Selectable("Drop here to move into this folder", false);
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload(kContentFilePayload)) {
                    const std::string_view source(static_cast<const char*>(payload->Data),
                        static_cast<std::size_t>(payload->DataSize));
                    moveFiles(source, m_relative);
                }
                ImGui::EndDragDropTarget();
            }
        }
        drawFolderList(allowMove, !playMode);
        constexpr char kDeletePopup[] = "Delete selected files?";
        if (!m_pendingDelete.empty() && !ImGui::IsPopupOpen(kDeletePopup)) {
            ImGui::OpenPopup(kDeletePopup);
        }
        if (ImGui::BeginPopupModal(kDeletePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (m_pendingDelete.size() == 1) {
                ImGui::Text("Permanently delete %s?", m_pendingDelete.front().c_str());
            } else {
                ImGui::Text("Permanently delete %zu files?", m_pendingDelete.size());
            }
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                m_pendingDelete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            ImGui::BeginDisabled(!allowMove || m_pendingDelete.empty());
            if (ImGui::Button("Delete")) {
                deletePendingFiles();
                m_pendingDelete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
        bool newFolder = false;
        if (ImGui::BeginPopupContextWindow("ContentContext",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            newFolder = ImGui::MenuItem("New Folder", nullptr, false, !playMode);
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
        drawNewFolderDialog(!playMode, newFolder);
    } else {
        ImGui::TextUnformatted("Create or open a project from File to browse files.");
    }
    ImGui::End();
}

void ContentPanel::drawBreadcrumb() {
    if (m_relative.empty()) {
        ImGui::TextDisabled("%s", pathText(m_project->root()).c_str());
    } else {
        ImGui::TextDisabled("%s", pathText(m_project->root() / m_relative).c_str());
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

void ContentPanel::drawFolderList(bool canMoveFiles, bool canReferenceAssets) {
    // Navigation can change m_relative while the current folder's rows are still drawing.
    const fs::path listedFolder = m_relative;
    const fs::path current = m_project->root() / listedFolder;
    if (m_selectionFolder != listedFolder) {
        m_selectionFolder = listedFolder;
        m_selectedFiles.clear();
        m_selectionAnchor.clear();
    }

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

    for (auto it = fs::directory_iterator(current, ec); !ec && it != fs::directory_iterator{}; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        const fs::path fileName = entry.path().filename();
        const auto name = pathText(fileName);
        if (name.starts_with('.') || isToolingEntry(name) || fileName.extension() == ".satimport" ||
            name.ends_with(".satimport.tmp") || name.ends_with(".satmat.tmp")) {
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
        const bool aIsParent = a.name == "..";
        if (aIsParent != (b.name == "..")) {
            return aIsParent;
        }
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory && !b.isDirectory;
        }
        return a.name.native() < b.name.native();
    });

    constexpr float kListThreshold = 0.15f;
    const bool listView = m_zoom <= kListThreshold;
    const float iconSize = listView
        ? ImGui::GetTextLineHeight()
        : lerp(40.0f, 96.0f, (m_zoom - kListThreshold) / (1.0f - kListThreshold));

    const ImTextureRef folderIcon((ImTextureID)(std::uintptr_t)m_folderIcon);
    const ImTextureRef returnIcon((ImTextureID)(std::uintptr_t)m_returnIcon);
    const ImTextureRef luaIcon(static_cast<ImTextureID>(m_luaIcon));

    const auto assetPathFor = [&listedFolder](const Entry& entry) {
        const auto encoded = (listedFolder / entry.name).generic_u8string();
        return std::string(encoded.begin(), encoded.end());
    };
    const auto selectFile = [&](const Entry& entry, const std::string& path) {
        const auto& io = ImGui::GetIO();
        if (io.KeyShift && !m_selectionAnchor.empty()) {
            const auto anchor = std::find_if(entries.begin(), entries.end(), [&](const Entry& candidate) {
                return !candidate.isDirectory && assetPathFor(candidate) == m_selectionAnchor;
            });
            if (anchor != entries.end()) {
                const auto clicked = entries.begin() + (&entry - entries.data());
                const auto [first, last] = std::minmax(anchor, clicked);
                if (!io.KeyCtrl) { m_selectedFiles.clear(); }
                for (auto it = first; it <= last; ++it) {
                    if (it->isDirectory) { continue; }
                    auto selectedPath = assetPathFor(*it);
                    if (std::find(m_selectedFiles.begin(), m_selectedFiles.end(), selectedPath) == m_selectedFiles.end()) {
                        m_selectedFiles.push_back(std::move(selectedPath));
                    }
                }
                if (m_select) { m_select(path); }
                return;
            }
        }
        if (!io.KeyCtrl) {
            m_selectedFiles.assign(1, path);
        } else if (std::erase(m_selectedFiles, path) == 0) {
            m_selectedFiles.push_back(path);
        }
        m_selectionAnchor = path;
        if (m_select) {
            if (!io.KeyCtrl || std::find(m_selectedFiles.begin(), m_selectedFiles.end(), path) != m_selectedFiles.end()) {
                m_select(path);
            } else if (m_selectedAsset == path) {
                m_select(m_selectedFiles.empty() ? std::string{} : m_selectedFiles.back());
            }
        }
    };
    const auto isHighlighted = [this](std::string_view path, bool directory) {
        if (!directory && (m_selectedFiles.empty() ? path == m_selectedAsset :
            std::find(m_selectedFiles.begin(), m_selectedFiles.end(), path) != m_selectedFiles.end())) { return true; }
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

    const auto handleDragDrop = [&](const Entry& entry, std::string_view assetPath, const std::string& name) {
        if (entry.isDirectory) {
            if (!canMoveFiles) { return; }
            if (ImGui::BeginDragDropTarget()) {
                const bool isParent = entry.name == "..";
                if (const auto* payload = ImGui::AcceptDragDropPayload(
                        kContentFilePayload, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                    if (isParent) {
                        constexpr double kParentHoverDelay = 0.65;
                        const int frame = ImGui::GetFrameCount();
                        const double now = ImGui::GetTime();
                        if (m_parentDragHoverFrame != frame - 1) {
                            m_parentDragHoverStart = now;
                            m_parentDragNavigated = false;
                        }
                        m_parentDragHoverFrame = frame;
                        if (!payload->IsDelivery() && now - m_parentDragHoverStart >= kParentHoverDelay) {
                            m_relative = listedFolder.parent_path();
                            m_parentDragHoverStart = now;
                            m_parentDragNavigated = true;
                        }
                    }
                    // A release just after navigation may still be over the next folder's parent row.
                    if (payload->IsDelivery() && (!isParent || !m_parentDragNavigated)) {
                        const std::string_view source(static_cast<const char*>(payload->Data),
                            static_cast<std::size_t>(payload->DataSize));
                        const auto target = isParent ? listedFolder.parent_path() : listedFolder / entry.name;
                        moveFiles(source, target);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        } else if (!isProtectedFile(assetPath) &&
                   (canMoveFiles || (canReferenceAssets &&
                       (isTextureAssetPath(assetPath) || isShaderAssetPath(assetPath) || isMeshAssetPath(assetPath)))) &&
                   ImGui::BeginDragDropSource()) {
            if (ImGui::GetDragDropPayload() == nullptr) {
                std::string paths(assetPath);
                paths.push_back('\0');
                if (canMoveFiles &&
                    std::find(m_selectedFiles.begin(), m_selectedFiles.end(), assetPath) != m_selectedFiles.end()) {
                    for (const auto& path : m_selectedFiles) {
                        if (path == assetPath || isProtectedFile(path)) { continue; }
                        paths.append(path);
                        paths.push_back('\0');
                    }
                }
                ImGui::SetDragDropPayload(kContentFilePayload, paths.data(), paths.size(), ImGuiCond_Once);
            }
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }
    };

    const auto drawFileMenu = [&](const Entry& entry, const std::string& path) {
        if (entry.isDirectory || !ImGui::BeginPopupContextItem("FileContext")) { return; }
        if (ImGui::MenuItem("Delete", nullptr, false, canMoveFiles && !isProtectedFile(path))) {
            if (std::find(m_selectedFiles.begin(), m_selectedFiles.end(), path) == m_selectedFiles.end()) {
                m_pendingDelete.assign(1, path);
            } else {
                m_pendingDelete = m_selectedFiles;
                std::erase_if(m_pendingDelete, isProtectedFile);
            }
        }
        ImGui::EndPopup();
    };

    if (listView) {
        for (const Entry& entry : entries) {
            const std::string name = pathText(entry.name);
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

            const auto assetPath = assetPathFor(entry);
            if (ImGui::Selectable(name.c_str(), isHighlighted(assetPath, entry.isDirectory))) {
                if (!entry.isDirectory) { selectFile(entry, assetPath); }
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

            handleDragDrop(entry, assetPath, name);
            drawFileMenu(entry, assetPath);
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
        const std::string name = pathText(entry.name);
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

        const auto assetPath = assetPathFor(entry);
        handleDragDrop(entry, assetPath, name);
        drawFileMenu(entry, assetPath);
        if (pressed && !entry.isDirectory) { selectFile(entry, assetPath); }
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
        const auto thumbnail = visible && !entry.isDirectory
            ? m_renderer->assetThumbnail(assetPath) : AssetThumbnail{};
        if (thumbnail.texture != 0) {
            const float width = iconSize * (std::min)(thumbnail.aspectRatio, 1.0f);
            const float height = iconSize / (std::max)(thumbnail.aspectRatio, 1.0f);
            const ImVec2 imageMin(iconMin.x + (iconSize - width) * 0.5f,
                                 iconMin.y + (iconSize - height) * 0.5f);
            draw->AddImage(ImTextureRef(static_cast<ImTextureID>(thumbnail.texture)),
                imageMin, ImVec2(imageMin.x + width, imageMin.y + height));
        } else if (isParent && m_returnIcon != 0) {
            draw->AddImage(returnIcon, iconMin, iconMax);
        } else if (entry.isDirectory && m_folderIcon != 0) {
            draw->AddImage(folderIcon, iconMin, iconMax);
        } else if (!entry.isDirectory && m_luaIcon != 0 && isLuaFile(entry.name)) {
            draw->AddImage(luaIcon, iconMin, iconMax);
        } else {
            draw->AddRectFilled(
                iconMin, iconMax, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
            const std::string_view glyph = name.empty() ? std::string_view("?") : std::string_view(name);
            unsigned int codepoint = 0;
            const char* glyphEnd = glyph.data() +
                ImTextCharFromUtf8(&codepoint, glyph.data(), glyph.data() + glyph.size());
            const ImVec2 glyphSize = ImGui::CalcTextSize(glyph.data(), glyphEnd);
            draw->AddText(
                ImVec2(
                    iconMin.x + (iconSize - glyphSize.x) * 0.5f,
                    iconMin.y + (iconSize - glyphSize.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text),
                glyph.data(),
                glyphEnd);
        }

        std::string label = name;
        if (ImGui::CalcTextSize(label.c_str()).x > iconSize) {
            while (!label.empty() && ImGui::CalcTextSize((label + "..").c_str()).x > iconSize) {
                std::size_t last = label.size() - 1;
                while (last > 0 && (static_cast<unsigned char>(label[last]) & 0xc0) == 0x80) {
                    --last;
                }
                label.resize(last);
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
