#include "CookedTexture.hpp"

#include "AssetLibrary.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <system_error>
#include <utility>

namespace sat {
namespace {

constexpr std::string_view kMagic = "SATBC7\x01";
constexpr std::string_view kFullPrefix = "CookedTextures/Full/";
constexpr std::string_view kPreviewPrefix = "CookedTextures/Preview/";
using Pixel = std::array<int, 4>;
using Block = std::array<Pixel, 16>;

int distance(const Pixel& a, const Pixel& b) {
    int result = 0;
    for (std::size_t c = 0; c < a.size(); ++c) {
        const int delta = a[c] - b[c];
        result += delta * delta;
    }
    return result;
}

Pixel quantizeEndpoint(const Pixel& endpoint, bool opaque) {
    const auto quantize = [&](int parity) {
        Pixel pixel{};
        for (std::size_t c = 0; c < pixel.size(); ++c) {
            pixel[c] = std::min(254 + parity, (endpoint[c] & ~1) + parity);
        }
        return pixel;
    };
    const auto odd = quantize(1);
    const auto even = quantize(0);
    return opaque || distance(endpoint, odd) < distance(endpoint, even) ? odd : even;
}

// BC7 mode 6 has two RGBA endpoints, one parity bit per endpoint, and
// 4-bit interpolation indices. The anchor's high index bit is implicit.
bool encodeBlock(const Block& block, std::string& bytes, std::uint64_t& colorError) {
    const auto farthestFrom = [&](const Pixel& origin) {
        int largest = -1;
        Pixel endpoint{};
        for (const auto& pixel : block) {
            const int error = distance(origin, pixel);
            if (error > largest) {
                largest = error;
                endpoint = pixel;
            }
        }
        return endpoint;
    };
    const bool opaque = std::ranges::all_of(block, [](const Pixel& pixel) { return pixel[3] == 255; });
    const auto first = farthestFrom(block.front());
    auto high = quantizeEndpoint(farthestFrom(first), opaque);
    auto low = quantizeEndpoint(first, opaque);
    const int span = distance(low, high);
    std::array<unsigned, 16> indices{};
    if (span != 0) {
        for (std::size_t i = 0; i < block.size(); ++i) {
            int projection = 0;
            for (std::size_t c = 0; c < low.size(); ++c) {
                projection += (block[i][c] - low[c]) * (high[c] - low[c]);
            }
            indices[i] = static_cast<unsigned>((std::clamp(projection, 0, span) * 15 + span / 2) / span);
        }
    }
    if (indices.front() >= 8) {
        std::swap(low, high);
        for (auto& index : indices) { index = 15 - index; }
    }

    constexpr int weights[] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
    for (std::size_t i = 0; i < block.size(); ++i) {
        const int weight = weights[indices[i]];
        for (std::size_t c = 0; c < 4; ++c) {
            const int decoded = ((64 - weight) * low[c] + weight * high[c] + 32) >> 6;
            const int error = decoded - block[i][c];
            if (c < 3) { colorError += error * error; }
            else if (error < -8 || error > 8) { return false; }
        }
    }

    std::array<unsigned char, 16> encoded{};
    unsigned offset = 0;
    unsigned pending = 0;
    unsigned pendingBits = 0;
    const auto write = [&](unsigned value, unsigned bits) {
        pending |= value << pendingBits;
        pendingBits += bits;
        if (pendingBits >= 8) {
            encoded[offset++] = static_cast<unsigned char>(pending);
            pending >>= 8;
            pendingBits -= 8;
        }
    };
    write(64, 7);
    for (std::size_t c = 0; c < low.size(); ++c) {
        write(static_cast<unsigned>(low[c] >> 1), 7);
        write(static_cast<unsigned>(high[c] >> 1), 7);
    }
    write(static_cast<unsigned>(low[0] & 1), 1);
    write(static_cast<unsigned>(high[0] & 1), 1);
    write(indices.front(), 3);
    for (std::size_t i = 1; i < indices.size(); ++i) { write(indices[i], 4); }
    bytes.append(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    return true;
}

std::size_t mipBytes(int width, int height) {
    return static_cast<std::size_t>((width + 3) / 4) * ((height + 3) / 4) * 16;
}

bool encodeMip(int width, int height, const std::vector<unsigned char>& pixels, std::string& bytes) {
    // Fall back to source import when single-subset BC7 loses too much detail.
    const auto blockCount = mipBytes(width, height) / 16;
    const auto maxColorError = blockCount * 16 * 3 * 100; // RGB RMS <= 10/255.
    std::uint64_t colorError = 0;
    bytes.reserve(bytes.size() + blockCount * 16);
    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            Block block{};
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 4; ++col) {
                    const auto texel = (static_cast<std::size_t>(std::min(y + row, height - 1)) * width +
                        std::min(x + col, width - 1)) * 4;
                    std::copy_n(pixels.data() + texel, 4, block[row * 4 + col].begin());
                }
            }
            if (!encodeBlock(block, bytes, colorError) || colorError > maxColorError) { return false; }
        }
    }
    return true;
}

void writeHeader(int width, int height, std::size_t count, const TextureData& source, std::string& bytes) {
    bytes.assign(kMagic);
    bytes.push_back(static_cast<char>(width & 255));
    bytes.push_back(static_cast<char>(width >> 8));
    bytes.push_back(static_cast<char>(height & 255));
    bytes.push_back(static_cast<char>(height >> 8));
    bytes.push_back(static_cast<char>(count));
    bytes.push_back(static_cast<char>(source.filter));
    bytes.push_back(static_cast<char>(source.wrap));
}

bool validSize(int width, int height) {
    return width >= 4 && height >= 4 && width <= 4096 && height <= 4096 && width % 4 == 0 && height % 4 == 0;
}

} // namespace

std::string cookedTexturePath(std::string_view source, bool preview) {
    std::string path(preview ? std::string_view("CookedTextures/Preview/") : kFullPrefix);
    path.append(source);
    return path;
}

std::string cookedEmbeddedTexturePath(std::string_view mesh, std::string_view image) {
    // Include the mesh path and its length to disambiguate repeated image names.
    std::string path = "CookedTextures/Embedded/" + std::to_string(mesh.size()) + '/';
    path.append(mesh).append("/").append(image);
    return path;
}

AssetImportSettings embeddedTextureSettings(const MeshData& mesh, std::string_view image) {
    AssetImportSettings settings;
    if (std::ranges::any_of(mesh.materials, [image](const MaterialData& material) {
        return material.transmission > 0.0f && material.metallicRoughnessTexture == image &&
            material.textureSampling[1].mipmaps;
    })) {
        settings.mipmaps = true;
        settings.filter = TextureFilter::Trilinear;
    }
    return settings;
}

bool cookTexture(const TextureData& source, std::string& cooked, std::string& preview) {
    if (!validSize(source.width, source.height) || source.pixels.empty() || source.blockCompressed) { return false; }
    const auto levels = source.mips.size() + 1;
    const auto maxLevels = std::bit_width(static_cast<unsigned>(std::max(source.width, source.height)));
    if (levels > static_cast<std::size_t>(maxLevels)) { return false; }
    int width = source.width;
    int height = source.height;
    std::string full;
    std::string coarse;
    writeHeader(width, height, levels, source, full);
    constexpr int kPreviewSize = 256;
    const bool needsPreview = std::max(width, height) > kPreviewSize;
    std::size_t previewOffset = 0;
    for (std::size_t level = 0; level < levels; ++level) {
        const auto& pixels = level == 0 ? source.pixels : source.mips[level - 1].pixels;
        if (pixels.size() != static_cast<std::size_t>(width) * height * 4 ||
            (level > 0 && (source.mips[level - 1].width != width || source.mips[level - 1].height != height))) {
            return false;
        }
        if (needsPreview && coarse.empty() && std::max(width, height) <= kPreviewSize && validSize(width, height)) {
            writeHeader(width, height, levels - level, source, coarse);
            previewOffset = full.size();
        }
        if (!encodeMip(width, height, pixels, full)) { return false; }
        width = std::max(1, width / 2);
        height = std::max(1, height / 2);
    }
    if (previewOffset != 0) { coarse.append(full, previewOffset); }
    cooked = std::move(full);
    preview = std::move(coarse);
    return true;
}

bool decodeCookedTexture(std::string_view bytes, TextureData& texture, std::string& error) {
    error.clear();
    constexpr auto kHeaderSize = kMagic.size() + 7;
    if (bytes.size() < kHeaderSize || bytes.size() > kMaxCookedTextureBytes || !bytes.starts_with(kMagic)) {
        error = "Invalid cooked texture header";
        return false;
    }
    const auto* fields = reinterpret_cast<const unsigned char*>(bytes.data() + kMagic.size());
    TextureData candidate;
    candidate.width = fields[0] | (fields[1] << 8);
    candidate.height = fields[2] | (fields[3] << 8);
    const auto levels = fields[4];
    if (!validSize(candidate.width, candidate.height) || levels == 0 ||
        levels > std::bit_width(static_cast<unsigned>(std::max(candidate.width, candidate.height))) ||
        fields[5] > static_cast<unsigned>(TextureFilter::Trilinear) ||
        fields[6] > static_cast<unsigned>(TextureWrap::Clamp)) {
        error = "Invalid cooked texture dimensions, mip count, or sampler";
        return false;
    }
    candidate.blockCompressed = true;
    candidate.filter = static_cast<TextureFilter>(fields[5]);
    candidate.wrap = static_cast<TextureWrap>(fields[6]);
    bytes.remove_prefix(kHeaderSize);
    int width = candidate.width;
    int height = candidate.height;
    for (unsigned level = 0; level < levels; ++level) {
        const auto size = mipBytes(width, height);
        if (bytes.size() < size) {
            error = "Truncated cooked texture";
            return false;
        }
        auto* pixels = &candidate.pixels;
        if (level > 0) {
            auto& mip = candidate.mips.emplace_back();
            mip.width = width;
            mip.height = height;
            pixels = &mip.pixels;
        }
        const auto* begin = reinterpret_cast<const unsigned char*>(bytes.data());
        pixels->assign(begin, begin + size);
        bytes.remove_prefix(size);
        width = std::max(1, width / 2);
        height = std::max(1, height / 2);
    }
    if (!bytes.empty()) {
        error = "Unexpected trailing cooked texture data";
        return false;
    }
    texture = std::move(candidate);
    return true;
}

namespace {

std::filesystem::path textureCacheDirectory(const std::filesystem::path& root, std::string_view path) {
    return root / ".cache" / "sat-textures-v1" / std::to_string(cookedTextureSourceHash(path));
}

std::string textureCacheFilename(std::uint64_t sourceHash, const AssetImportSettings& settings) {
    const auto settingsHash = cookedTextureSourceHash(serializeImportSettings(settings));
    return std::to_string(sourceHash) + "-" + std::to_string(settingsHash);
}

} // namespace

std::uint64_t cookedTextureSourceHash(std::string_view bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

AssetReader projectRenderAssetReader(const std::filesystem::path& root) {
    return [root, reader = fileAssetReader(root)](std::string_view path, std::string& bytes, std::string& error) {
        const bool preview = path.starts_with(kPreviewPrefix);
        if (!preview && !path.starts_with(kFullPrefix)) {
            return path.starts_with("CookedTextures/") ? AssetReadResult::Missing : reader(path, bytes, error);
        }
        path.remove_prefix(preview ? kPreviewPrefix.size() : kFullPrefix.size());
        if (!isTextureAssetPath(path)) { return AssetReadResult::Missing; }
        const auto directory = textureCacheDirectory(root, path);
        std::error_code ec;
        if (!std::filesystem::is_directory(directory, ec)) { return AssetReadResult::Missing; }
        AssetImportSettings settings;
        const auto metadata = reader(std::string(path) + ".satimport", bytes, error);
        if (metadata == AssetReadResult::Error ||
            (metadata == AssetReadResult::Found && !parseImportSettings(bytes, settings, error)) ||
            reader(path, bytes, error) != AssetReadResult::Found) {
            return AssetReadResult::Missing;
        }
        auto location = directory / textureCacheFilename(cookedTextureSourceHash(bytes), settings);
        if (preview) { location += ".preview"; }
        std::ifstream stream(location, std::ios::binary | std::ios::ate);
        const auto size = stream.tellg();
        bytes.clear();
        if (size <= 0 || size > static_cast<std::streamoff>(kMaxCookedTextureBytes)) { return AssetReadResult::Missing; }
        stream.seekg(0);
        bytes.resize(static_cast<std::size_t>(size));
        return stream.read(bytes.data(), static_cast<std::streamsize>(size)) ? AssetReadResult::Found : AssetReadResult::Missing;
    };
}

void cacheCookedTexture(const std::filesystem::path& root, std::string_view path, std::uint64_t sourceHash,
    const AssetImportSettings& settings, const TextureData& texture) {
    if (!isTextureAssetPath(path)) { return; }
    const auto destination = textureCacheDirectory(root, path) / textureCacheFilename(sourceHash, settings);
    std::error_code ec;
    if (std::filesystem::is_regular_file(destination, ec)) {
        auto previewPath = destination;
        previewPath += ".preview";
        if (texture.mips.empty() || std::max(texture.width, texture.height) <= 256 ||
            std::filesystem::is_regular_file(previewPath, ec)) { return; }
    }
    std::string cooked, preview;
    if (!cookTexture(texture, cooked, preview)) { return; }
    auto staging = destination;
    static std::atomic<unsigned> sequence = 0; // Unique temporary paths for concurrent imports.
    staging += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        "-" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + ".tmp";
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec || !std::filesystem::create_directory(staging, ec)) { return; }
    const auto write = [&](std::string_view bytes, const std::filesystem::path& target) {
        const auto temporary = staging / "texture";
        std::ofstream stream(temporary, std::ios::binary);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        if (stream) { std::filesystem::rename(temporary, target, ec); }
        std::filesystem::remove(temporary, ec);
    };
    // Publish the small chain first. Missing or interrupted cache writes remain
    // optional and fall back to the source importer.
    if (!preview.empty()) {
        auto previewPath = destination;
        previewPath += ".preview";
        write(preview, previewPath);
    }
    write(cooked, destination);
    std::filesystem::remove(staging, ec);
}

} // namespace sat
