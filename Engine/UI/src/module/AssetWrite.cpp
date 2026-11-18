#include "AssetWrite.hpp"
#include <fstream>
#include <system_error>
namespace sat {
bool writeAsset(const std::filesystem::path& destination, std::string_view path, std::string_view bytes, std::string& error) {
    auto temp = destination;
    temp += ".tmp";
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(temp, ec);
    if ((ec && ec != std::errc::no_such_file_or_directory) ||
        (std::filesystem::exists(status) && !std::filesystem::is_regular_file(status))) {
        error = "Cannot use temporary file for " + std::string(path);
        return false;
    }
    ec.clear();
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) { error = "Cannot open temporary file for " + std::string(path); return false; }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (out) { std::filesystem::rename(temp, destination, ec); }
    if (!out || ec) {
        error = "Cannot save " + std::string(path) + (ec ? ": " + ec.message() : "");
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}
}
