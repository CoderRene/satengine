#include "AssetData.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <climits>
#include <memory>
#include <utility>

namespace sat {
bool decodeTexture(std::string_view bytes, TextureData& texture, std::string& error) {
    error.clear();
    if (bytes.empty() || bytes.size() > kMaxAssetBytes || bytes.size() > INT_MAX) {
        error = "Invalid image size (1 byte to 64 MiB required)";
        return false;
    }
    if (!bytes.starts_with("\x89PNG\r\n\x1a\n") &&
        !(bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff &&
          static_cast<unsigned char>(bytes[1]) == 0xd8)) {
        error = "Only PNG and JPEG images are supported";
        return false;
    }
    const auto* encoded = reinterpret_cast<const stbi_uc*>(bytes.data());
    int width = 0;
    int height = 0;
    int channels = 0;
    const int size = static_cast<int>(bytes.size());
    if (!stbi_info_from_memory(encoded, size, &width, &height, &channels) ||
        width < 1 || height < 1 || width > 4096 || height > 4096) {
        error = "Invalid image or dimensions outside 1..4096";
        return false;
    }
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(encoded, size, &width, &height, &channels, 4), stbi_image_free);
    if (!pixels) {
        const char* reason = stbi_failure_reason();
        error = reason != nullptr ? reason : "Image decode failed";
        return false;
    }
    if (width < 1 || height < 1 || width > 4096 || height > 4096) {
        error = "Decoded image dimensions exceed 4096";
        return false;
    }
    TextureData candidate;
    candidate.width = width;
    candidate.height = height;
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    candidate.pixels.assign(pixels.get(), pixels.get() + count);
    texture = std::move(candidate);
    return true;
}
} // namespace sat
