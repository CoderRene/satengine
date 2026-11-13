#include "AssetImportSettings.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sat {
bool parseImportSettings(std::string_view text, AssetImportSettings& settings, std::string& error) {
    error.clear();
    if (text.size() > 4096) { error = "Import metadata exceeds 4096 bytes"; return false; }
    std::istringstream input{std::string(text)};
    input.imbue(std::locale::classic());
    std::string magic;
    int version = 0;
    int filter = 0;
    int wrap = 0;
    AssetImportSettings candidate;
    if (text.size() > 4096 || !(input >> magic >> version >> candidate.scale >> candidate.rightHanded >> candidate.flipV
        >> candidate.maximumSize >> filter >> wrap >> candidate.mipmaps) || magic != "SatImport" || (version != 1 && version != 2) ||
        !std::isfinite(candidate.scale) || candidate.scale <= 0 || candidate.maximumSize < 32 || candidate.maximumSize > 4096 ||
        (candidate.maximumSize & (candidate.maximumSize - 1)) != 0 || filter < 0 || filter > 2 || wrap < 0 || wrap > 1) {
        error = "Invalid SatImport settings or unsupported version";
        return false;
    }
    if (version == 2) {
        int audioMode = 0;
        if (!(input >> audioMode) || audioMode < 0 || audioMode > 2) {
            error = "Invalid audio import mode";
            return false;
        }
        candidate.audioLoadMode = static_cast<AudioLoadMode>(audioMode);
    }
    input >> std::ws;
    if (!input.eof()) { error = "Unexpected data after import settings"; return false; }
    candidate.filter = static_cast<TextureFilter>(filter);
    candidate.wrap = static_cast<TextureWrap>(wrap);
    settings = candidate;
    return true;
}
std::string serializeImportSettings(const AssetImportSettings& settings) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    const bool extended = settings.audioLoadMode != AudioLoadMode::Auto;
    output << (extended ? "SatImport 2\n" : "SatImport 1\n") << std::setprecision(std::numeric_limits<float>::max_digits10)
        << settings.scale << ' ' << settings.rightHanded << ' ' << settings.flipV << '\n'
        << settings.maximumSize << ' ' << static_cast<int>(settings.filter) << ' '
        << static_cast<int>(settings.wrap) << ' ' << settings.mipmaps << '\n';
    if (extended) { output << static_cast<int>(settings.audioLoadMode) << '\n'; }
    return output.str();
}
bool loadImportSettings(const std::filesystem::path& source, AssetImportSettings& settings, std::string& error) {
    auto path = source; path += ".satimport";
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory || (!ec && status.type() == std::filesystem::file_type::not_found)) {
        settings = {}; error.clear(); return true;
    }
    if (ec) { error = ec.message(); return false; }
    if (!std::filesystem::is_regular_file(path, ec) || ec) { error = "Import metadata is not a readable regular file"; return false; }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || file.tellg() > 4096) { error = "Cannot read bounded import metadata"; return false; }
    std::string text(static_cast<std::size_t>(file.tellg()), '\0');
    file.seekg(0);
    if (!file.read(text.data(), static_cast<std::streamsize>(text.size()))) { error = "Cannot read import metadata"; return false; }
    return parseImportSettings(text, settings, error);
}
bool saveImportSettings(const std::filesystem::path& source, const AssetImportSettings& settings, std::string& error) {
    const auto text = serializeImportSettings(settings);
    AssetImportSettings validated;
    if (!parseImportSettings(text, validated, error)) { return false; }
    auto destination = source; destination += ".satimport";
    auto temporary = destination; temporary += ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    std::error_code ec;
    if (file) {
#if defined(_WIN32)
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        }
#else
        std::filesystem::rename(temporary, destination, ec);
#endif
        if (!ec) { error.clear(); return true; }
    }
    error = "Cannot replace import metadata";
    if (ec) { error += ": " + ec.message(); }
    std::filesystem::remove(temporary, ec);
    return false;
}
}
