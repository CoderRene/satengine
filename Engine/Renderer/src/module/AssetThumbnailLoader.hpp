#pragma once

#include "AssetData.hpp"

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

namespace sat {

class MeshPass;

// Owned by the render thread. The worker decodes textures and prepares meshes
// without using the immediate context. Clearing drops obsolete results.
class AssetThumbnailLoader {
public:
    struct Result {
        std::string path;
        TextureData image;
        std::shared_ptr<MeshPass> meshPass;
    };

    using PrepareMesh = std::function<std::shared_ptr<MeshPass>(const AssetReader&, std::string_view)>;

    // The reader and preparation callback run on the worker.
    void setReader(AssetReader reader, PrepareMesh prepareMesh = {});
    void clear();
    void stop();
    // Returns whether the loader handles this request, even if the queue is full.
    [[nodiscard]] bool request(std::string_view path);
    [[nodiscard]] std::optional<Result> takeReady();

private:
    struct Source {
        AssetReader reader;
        PrepareMesh prepareMesh;
    };
    struct Job {
        std::string path;
        std::optional<Result> result;
    };

    void run(std::stop_token stop);

    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::shared_ptr<const Source> m_source;
    std::deque<std::shared_ptr<Job>> m_jobs;
    std::jthread m_worker; // Joins before the queue and synchronization objects are destroyed.
};

}
