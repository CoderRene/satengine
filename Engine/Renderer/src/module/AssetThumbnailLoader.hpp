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
#include <vector>

namespace sat {

class MeshPass;

// Owned by the render thread. Workers decode textures and prepare meshes
// without using the immediate context. Clearing drops obsolete results.
class AssetThumbnailLoader {
public:
    struct Result {
        std::string path;
        TextureData image;
        std::shared_ptr<MeshPass> meshPass;
        std::vector<std::string> dependencies;
    };

    using PrepareMesh = std::function<std::shared_ptr<MeshPass>(const AssetReader&, std::string_view)>;

    enum class Kind { Texture, Mesh };

    // The reader must support concurrent calls. Mesh preparation runs serially.
    void setReader(AssetReader reader, PrepareMesh prepareMesh = {});
    void clear();
    void stop();
    // Returns whether the loader handles this request, even if the queue is full.
    [[nodiscard]] bool request(std::string_view path);
    [[nodiscard]] std::optional<Result> takeReady(Kind kind);

private:
    struct Source {
        AssetReader reader;
        PrepareMesh prepareMesh;
    };
    struct Job {
        std::string path;
        std::optional<Result> result;
        bool started = false;
    };

    using Queue = std::deque<std::shared_ptr<Job>>;
    void run(std::stop_token stop, Kind kind);
    [[nodiscard]] Queue& queue(Kind kind);

    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::shared_ptr<const Source> m_source;
    Queue m_textureJobs;
    Queue m_meshJobs;
    // Joins before the queues and synchronization objects are destroyed.
    std::jthread m_textureWorker;
    std::jthread m_secondTextureWorker;
    std::jthread m_meshWorker;
};

}
