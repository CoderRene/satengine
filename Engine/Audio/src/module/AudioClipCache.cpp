#include "AudioClipCache.hpp"

#include "AssetData.hpp"

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace sat {
namespace {
constexpr std::size_t kDecodeThreshold = 256 * 1024;
// Streaming ends at EOF. Avoid scanning a whole MP3 for its length.
constexpr ma_uint32 kStreamFlags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_UNKNOWN_LENGTH;

struct AudioFile {
    std::string_view bytes;
    std::size_t cursor = 0;
};
} // namespace

AudioClipCache::AudioClipCache() {
    m_vfs.cache = this;
    m_vfs.callbacks.onOpen = [](ma_vfs* vfs, const char* path, ma_uint32 mode, ma_vfs_file* file) {
        if (mode != MA_OPEN_MODE_READ) {
            return MA_INVALID_ARGS;
        }
        try {
            auto& cache = *static_cast<Vfs*>(vfs)->cache;
            const std::scoped_lock lock(cache.m_mutex);
            const auto found = cache.m_clips.find(std::string_view(path));
            if (found == cache.m_clips.end()) {
                return MA_DOES_NOT_EXIST;
            }
            *file = new (std::nothrow) AudioFile{found->second.encoded};
            return *file ? MA_SUCCESS : MA_OUT_OF_MEMORY;
        } catch (...) {
            return MA_IO_ERROR;
        }
    };
    m_vfs.callbacks.onClose = [](ma_vfs*, ma_vfs_file file) {
        delete static_cast<AudioFile*>(file);
        return MA_SUCCESS;
    };
    m_vfs.callbacks.onRead = [](ma_vfs*, ma_vfs_file file, void* out, std::size_t size, std::size_t* read) {
        auto& opened = *static_cast<AudioFile*>(file);
        *read = std::min(size, opened.bytes.size() - opened.cursor);
        std::memcpy(out, opened.bytes.data() + opened.cursor, *read);
        opened.cursor += *read;
        return MA_SUCCESS;
    };
    m_vfs.callbacks.onSeek = [](ma_vfs*, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin) {
        auto& opened = *static_cast<AudioFile*>(file);
        const auto size = static_cast<ma_int64>(opened.bytes.size());
        ma_int64 base = 0;
        if (origin == ma_seek_origin_current) {
            base = static_cast<ma_int64>(opened.cursor);
        } else if (origin == ma_seek_origin_end) {
            base = size;
        }
        if (offset < -base || offset > size - base) {
            return MA_BAD_SEEK;
        }
        opened.cursor = static_cast<std::size_t>(base + offset);
        return MA_SUCCESS;
    };
    m_vfs.callbacks.onTell = [](ma_vfs*, ma_vfs_file file, ma_int64* cursor) {
        *cursor = static_cast<ma_int64>(static_cast<AudioFile*>(file)->cursor);
        return MA_SUCCESS;
    };
    m_vfs.callbacks.onInfo = [](ma_vfs*, ma_vfs_file file, ma_file_info* info) {
        info->sizeInBytes = static_cast<AudioFile*>(file)->bytes.size();
        return MA_SUCCESS;
    };
}

const std::string* AudioClipCache::load(ma_resource_manager& manager, const AssetReader& reader,
    std::string_view path, ma_uint32& flags, std::string& error) {
    if (!isAudioAssetPath(path)) {
        error = "Invalid or unsupported audio path: " + std::string(path);
        return nullptr;
    }

    // Only the engine thread writes the table. Worker lookups lock against writes.
    auto found = m_clips.find(path);
    if (found != m_clips.end()) {
        flags = found->second.decoded ? MA_SOUND_FLAG_DECODE : kStreamFlags;
        return &found->first;
    }

    std::string bytes;
    std::string detail;
    const auto read = reader ? reader(path, bytes, detail) : AssetReadResult::Missing;
    if (read != AssetReadResult::Found || bytes.empty() || bytes.size() > kMaxAssetBytes) {
        if (read == AssetReadResult::Missing) {
            detail = "clip is missing";
        } else if (read == AssetReadResult::Error && detail.empty()) {
            detail = "could not read clip";
        } else if (read == AssetReadResult::Found) {
            detail = "clip is empty or exceeds 64 MiB";
        }
        error = "Audio " + std::string(path) + ": " + detail;
        return nullptr;
    }

    AssetImportSettings settings;
    std::string metadata;
    const auto metadataResult = reader(std::string(path) + ".satimport", metadata, detail);
    if (metadataResult == AssetReadResult::Error ||
        (metadataResult == AssetReadResult::Found && !parseImportSettings(metadata, settings, detail))) {
        error = "Audio import settings for " + std::string(path) + ": " + detail;
        return nullptr;
    }
    const bool decode = settings.audioLoadMode == AudioLoadMode::Decode ||
        (settings.audioLoadMode == AudioLoadMode::Auto && bytes.size() <= kDecodeThreshold);
    Clip clip;
    auto config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_uint64 frames = 0;
    if (decode) {
        void* pcm = nullptr;
        const auto result = ma_decode_memory(bytes.data(), bytes.size(), &config, &frames, &pcm);
        clip.decoded.reset(pcm);
        if (result != MA_SUCCESS || frames == 0) {
            error = "Cannot decode audio " + std::string(path) + ": " +
                (result == MA_SUCCESS ? "clip contains no samples" : ma_result_description(result));
            return nullptr;
        }
    }
    clip.encoded = std::move(bytes);
    {
        const std::scoped_lock lock(m_mutex);
        found = m_clips.emplace(std::string(path), std::move(clip)).first;
    }

    // Register the decoded samples directly because a registered encoded
    // buffer takes precedence over the DECODE flag in miniaudio.
    const auto& [name, cached] = *found;
    const ma_result result = decode
        ? ma_resource_manager_register_decoded_data(&manager, name.c_str(), cached.decoded.get(),
            frames, config.format, config.channels, config.sampleRate)
        : ma_resource_manager_register_encoded_data(&manager, name.c_str(), cached.encoded.data(), cached.encoded.size());
    if (result != MA_SUCCESS) {
        const std::scoped_lock lock(m_mutex);
        m_clips.erase(found);
        error = "Cannot load audio " + std::string(path) + ": " + ma_result_description(result);
        return nullptr;
    }
    flags = decode ? MA_SOUND_FLAG_DECODE : kStreamFlags;
    return &name;
}

void AudioClipCache::clear(ma_resource_manager& manager) {
    for (const auto& clip : m_clips) {
        ma_resource_manager_unregister_data(&manager, clip.first.c_str());
    }
    const std::scoped_lock lock(m_mutex);
    m_clips.clear();
}

} // namespace sat
