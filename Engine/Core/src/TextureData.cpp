#include "AssetData.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <climits>
#include <istream>
#include <memory>
#include <streambuf>
#include <utility>

namespace sat {
namespace {
bool supportedImage(std::string_view bytes) {
    return bytes.starts_with("\x89PNG\r\n\x1a\n") || bytes.starts_with("\xff\xd8");
}

bool validDimensions(int width, int height) {
    return width >= 1 && height >= 1 && width <= 4096 && height <= 4096;
}
}

bool readTextureDimensions(std::istream& stream, int& width, int& height, std::string& error) {
    error.clear();
    auto* buffer = stream.rdbuf();
    char signature[8];
    const auto count = buffer->sgetn(signature, sizeof(signature));
    if (!supportedImage(std::string_view(signature, static_cast<std::size_t>(count)))) {
        error = "Only PNG and JPEG images are supported";
        return false;
    }
    if (!stream.seekg(0)) { error = "Cannot seek image header"; return false; }

    constexpr stbi_io_callbacks callbacks{
        [](void* user, char* data, int size) {
            return static_cast<int>(static_cast<std::streambuf*>(user)->sgetn(data, size));
        },
        [](void* user, int offset) {
            static_cast<std::streambuf*>(user)->pubseekoff(offset, std::ios::cur, std::ios::in);
        },
        [](void* user) -> int {
            return static_cast<std::streambuf*>(user)->sgetc() == std::char_traits<char>::eof();
        }
    };
    int sourceWidth = 0;
    int sourceHeight = 0;
    if (!stbi_info_from_callbacks(&callbacks, buffer, &sourceWidth, &sourceHeight, nullptr) ||
        !validDimensions(sourceWidth, sourceHeight)) {
        error = "Invalid image or dimensions outside 1..4096";
        return false;
    }
    width = sourceWidth;
    height = sourceHeight;
    return true;
}

bool decodeTexture(std::string_view bytes, TextureData& texture, std::string& error) {
    error.clear();
    if (bytes.empty() || bytes.size() > kMaxAssetBytes || bytes.size() > INT_MAX) {
        error = "Invalid image size (1 byte to 64 MiB required)";
        return false;
    }
    if (!supportedImage(bytes)) {
        error = "Only PNG and JPEG images are supported";
        return false;
    }
    const auto* encoded = reinterpret_cast<const stbi_uc*>(bytes.data());
    int width = 0;
    int height = 0;
    int channels = 0;
    const int size = static_cast<int>(bytes.size());
    if (!stbi_info_from_memory(encoded, size, &width, &height, &channels) ||
        !validDimensions(width, height)) {
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
    if (!validDimensions(width, height)) {
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
