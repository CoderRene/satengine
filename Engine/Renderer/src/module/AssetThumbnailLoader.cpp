#include "AssetThumbnailLoader.hpp"

#include "AssetImportSettings.hpp"
#include "AssetImporter.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <utility>

namespace sat {

static TextureData decodeTextureThumbnail(const AssetReader& reader, std::string_view path) {
    constexpr int kPreviewSize = 64;
    AssetImportSettings settings;
    settings.maximumSize = kPreviewSize;
    TextureData image;
    std::string bytes;
    std::string error;
    if (!reader || reader(path, bytes, error) != AssetReadResult::Found ||
        !importTexture(bytes, settings, image, error)) { return {}; }
    return image;
}

void AssetThumbnailLoader::setReader(AssetReader reader, PrepareMesh prepareMesh) {
    auto source = reader ? std::make_shared<const Source>(Source{std::move(reader), std::move(prepareMesh)}) : nullptr;
    const std::lock_guard lock(m_mutex);
    m_source = std::move(source);
    m_jobs.clear();
}

void AssetThumbnailLoader::clear() {
    const std::lock_guard lock(m_mutex);
    m_jobs.clear();
}

void AssetThumbnailLoader::stop() {
    clear();
    m_worker = {};
    m_source.reset();
}

bool AssetThumbnailLoader::request(std::string_view path) {
    constexpr std::size_t kMaxJobs = 8;
    {
        const std::lock_guard lock(m_mutex);
        if (!m_source) { return false; }
        if (m_jobs.size() >= kMaxJobs ||
            std::ranges::any_of(m_jobs, [path](const auto& job) { return job->path == path; })) { return true; }
        if (!m_worker.joinable()) {
            m_worker = std::jthread([this](std::stop_token stop) { run(stop); });
        }
        m_jobs.push_back(std::make_shared<Job>(Job{std::string(path), std::nullopt}));
    }
    m_wake.notify_one();
    return true;
}

std::optional<AssetThumbnailLoader::Result> AssetThumbnailLoader::takeReady() {
    const std::lock_guard lock(m_mutex);
    if (m_jobs.empty() || !m_jobs.front()->result) { return std::nullopt; }
    auto job = std::move(m_jobs.front());
    m_jobs.pop_front();
    m_wake.notify_one();
    job->result->path = std::move(job->path);
    return std::move(job->result);
}

void AssetThumbnailLoader::run(std::stop_token stop) {
    std::unique_lock lock(m_mutex);
    while (m_wake.wait(lock, stop, [this] { return !m_jobs.empty() && !m_jobs.front()->result; })) {
        if (stop.stop_requested()) { break; }
        // One worker finishes jobs in order. Holding the job and reader here
        // lets the render thread reset the queue without waiting for file I/O.
        const auto job = m_jobs.front();
        const auto source = m_source;
        lock.unlock();
        Result result;
        try {
            if (isTextureAssetPath(job->path)) {
                result.image = decodeTextureThumbnail(source->reader, job->path);
            } else if (source->prepareMesh) {
                result.meshPass = source->prepareMesh(source->reader, job->path);
            }
        } catch (const std::exception&) {
            // An unreadable preview renders as the normal file icon.
        }
        lock.lock();
        // Only one prepared preview is retained. Dispose of obsolete resources
        // on the worker without holding the queue lock.
        if (!m_jobs.empty() && m_jobs.front() == job) {
            job->result.emplace(std::move(result));
        } else {
            lock.unlock();
            result = {};
            lock.lock();
        }
    }
}

}
