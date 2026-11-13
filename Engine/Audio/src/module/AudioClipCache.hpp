#pragma once

#include "AssetLibrary.hpp"

#include <miniaudio.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace sat {

// The VFS runs on miniaudio's resource worker. Encoded bytes stay immutable
// and at a stable address until all sounds and decoder jobs have been released.
class AudioClipCache {
public:
    AudioClipCache();
    [[nodiscard]] ma_vfs* vfs() { return &m_vfs; }
    [[nodiscard]] const std::string* load(ma_resource_manager& manager, const AssetReader& reader,
        std::string_view path, ma_uint32& flags, std::string& error);
    void clear(ma_resource_manager& manager);

private:
    struct Vfs {
        ma_vfs_callbacks callbacks{};
        AudioClipCache* cache = nullptr;
    };

    struct StringHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view value) const { return std::hash<std::string_view>{}(value); }
    };

    struct PcmDeleter {
        void operator()(void* data) const { ma_free(data, nullptr); }
    };
    struct Clip {
        std::string encoded;
        std::unique_ptr<void, PcmDeleter> decoded;
    };

    Vfs m_vfs;
    std::mutex m_mutex;
    std::unordered_map<std::string, Clip, StringHash, std::equal_to<>> m_clips;
};

} // namespace sat
