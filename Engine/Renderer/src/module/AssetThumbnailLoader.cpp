#include "AssetThumbnailLoader.hpp"

#include "AssetImportSettings.hpp"
#include "AssetImporter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <utility>

namespace sat {

static TextureData decodeTextureThumbnail(const AssetReader& reader, std::string_view path) {
    constexpr int kPreviewSize = 64;
    AssetImportSettings settings;
    settings.maximumSize = kPreviewSize;
    settings.mipmaps = false;
    TextureData image;
    std::string bytes;
    std::string error;
    if (!reader || reader(path, bytes, error) != AssetReadResult::Found ||
        !importTexture(bytes, settings, image, error)) { return {}; }
    if (!image.hdrPixels.empty()) {
        image.pixels.resize(image.hdrPixels.size());
        for (std::size_t i = 0; i < image.hdrPixels.size(); i += 4) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const float linear = image.hdrPixels[i + channel];
                const float mapped = linear / (1.0f + linear);
                const float srgb = mapped <= 0.0031308f ? 12.92f * mapped :
                    1.055f * std::pow(mapped, 1.0f / 2.4f) - 0.055f;
                image.pixels[i + channel] = static_cast<unsigned char>(std::lround(255.0f * srgb));
            }
            image.pixels[i + 3] = 255;
        }
        image.hdrPixels = {};
    }
    return image;
}

void AssetThumbnailLoader::setReader(AssetReader reader, PrepareMesh prepareMesh) {
    auto source = reader ? std::make_shared<const Source>(Source{std::move(reader), std::move(prepareMesh)}) : nullptr;
    const std::lock_guard lock(m_mutex);
    m_source = std::move(source);
    m_textureJobs.clear();
    m_meshJobs.clear();
}

void AssetThumbnailLoader::clear() {
    const std::lock_guard lock(m_mutex);
    m_textureJobs.clear();
    m_meshJobs.clear();
}

void AssetThumbnailLoader::stop() {
    clear();
    m_textureWorker = {};
    m_secondTextureWorker = {};
    m_meshWorker = {};
    m_source.reset();
}

AssetThumbnailLoader::Queue& AssetThumbnailLoader::queue(Kind kind) {
    return kind == Kind::Texture ? m_textureJobs : m_meshJobs;
}

bool AssetThumbnailLoader::request(std::string_view path) {
    constexpr std::size_t kMaxJobs = 8;
    const auto kind = isTextureAssetPath(path) ? Kind::Texture : Kind::Mesh;
    {
        const std::lock_guard lock(m_mutex);
        if (!m_source) { return false; }
        auto& jobs = queue(kind);
        if (jobs.size() >= kMaxJobs ||
            std::ranges::any_of(jobs, [path](const auto& job) { return job->path == path; })) { return true; }
        auto& worker = kind == Kind::Texture ? m_textureWorker : m_meshWorker;
        if (!worker.joinable()) {
            worker = std::jthread([this, kind](std::stop_token stop) { run(stop, kind); });
        }
        if (kind == Kind::Texture && jobs.size() == 1 && !m_secondTextureWorker.joinable()) {
            m_secondTextureWorker = std::jthread([this](std::stop_token stop) { run(stop, Kind::Texture); });
        }
        jobs.push_back(std::make_shared<Job>(Job{std::string(path), std::nullopt}));
    }
    m_wake.notify_all();
    return true;
}

std::optional<AssetThumbnailLoader::Result> AssetThumbnailLoader::takeReady(Kind kind) {
    const std::lock_guard lock(m_mutex);
    auto& jobs = queue(kind);
    if (jobs.empty() || !jobs.front()->result) { return std::nullopt; }
    auto job = std::move(jobs.front());
    jobs.pop_front();
    if (kind == Kind::Mesh) { m_wake.notify_all(); }
    job->result->path = std::move(job->path);
    return std::move(job->result);
}

void AssetThumbnailLoader::run(std::stop_token stop, Kind kind) {
    auto& jobs = queue(kind);
    std::unique_lock lock(m_mutex);
    while (!stop.stop_requested()) {
        std::shared_ptr<Job> job;
        if (!m_wake.wait(lock, stop, [&] {
            if (kind == Kind::Mesh) {
                // Keep at most one prepared mesh and its GPU resources.
                if (!jobs.empty() && !jobs.front()->started) { job = jobs.front(); }
            } else {
                const auto pending = std::ranges::find_if(jobs, [](const auto& entry) { return !entry->started; });
                if (pending != jobs.end()) { job = *pending; }
            }
            return static_cast<bool>(job);
        }) || stop.stop_requested()) { break; }
        job->started = true;
        const auto source = m_source;
        lock.unlock();
        Result result;
        try {
            if (kind == Kind::Texture) {
                result.image = decodeTextureThumbnail(source->reader, job->path);
            } else if (source->prepareMesh) {
                const AssetReader track = [&](std::string_view path, std::string& bytes, std::string& error) {
                    if (path != job->path && std::ranges::find(result.dependencies, path) == result.dependencies.end()) {
                        result.dependencies.emplace_back(path);
                    }
                    return source->reader(path, bytes, error);
                };
                result.meshPass = source->prepareMesh(track, job->path);
            }
        } catch (const std::exception&) {
            // An unreadable preview renders as the normal file icon.
        }
        lock.lock();
        // Clearing drops jobs from both queues, including in-flight previews.
        if (std::ranges::find(jobs, job) != jobs.end()) {
            job->result.emplace(std::move(result));
        } else {
            lock.unlock();
            result = {};
            lock.lock();
        }
    }
}

}
