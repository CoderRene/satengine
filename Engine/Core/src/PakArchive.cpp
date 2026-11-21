#include "PakArchive.hpp"
#include "module/Pak/PakCrypto.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <limits>
#include <utility>
#include <mutex>

namespace sat {
namespace {
constexpr std::uint64_t kMaxBytes = 2ULL * 1024 * 1024 * 1024;
constexpr std::string_view kMagic = "SATPAK01";
bool validPath(std::string_view name) {
    if (name.empty() || name.size() > 4096 || name.front() == '/' ||
        name.find_first_of("\\:\0", 0, 3) != std::string_view::npos) { return false; }
    while (!name.empty()) {
        const auto slash = name.find('/');
        const auto part = name.substr(0, slash);
        if (part.empty() || part == "." || part == "..") { return false; }
        if (slash == std::string_view::npos) { return true; }
        name.remove_prefix(slash + 1);
    }
    return false;
}
void writeInteger(std::ostream& stream, std::uint64_t value, int size) {
    for (int i = 0; i < size; ++i) { stream.put(static_cast<char>((value >> (i * 8)) & 255)); }
}
std::uint64_t readInteger(std::istream& stream, int size) {
    std::uint64_t result = 0;
    for (int i = 0; i < size; ++i) {
        const int byte = stream.get();
        if (byte == std::char_traits<char>::eof()) { return 0; }
        result |= static_cast<std::uint64_t>(static_cast<unsigned char>(byte)) << (i * 8);
    }
    return result;
}

constexpr std::size_t kHeaderBytes = 52;
constexpr std::size_t kEnvelopeBytes = kHeaderBytes + 16;
constexpr std::uint64_t kChunkBytes = 8 * 1024 * 1024;
constexpr std::uint64_t kIndexLimit = 64 * 1024 * 1024;
constexpr std::uint32_t kPartLimit = 4096;
constexpr std::uint64_t kPayloadPartBytes = kChunkBytes + kEnvelopeBytes;
constexpr std::uint64_t kPayloadCapacity = (kPartLimit - 1) * kPayloadPartBytes;
constexpr std::uint64_t kRecordLimit = kIndexLimit / 13 + kPartLimit;

struct ChunkCursor {
    std::uint32_t part = 1;
    std::uint32_t offset = 0;
    std::uint32_t ordinal = 1;

    // Sorted entries are segmented independently. A payload part contains
    // whole encrypted records and a record never contains two entries.
    ChunkCursor advance(std::uint64_t bytes) {
        const auto recordBytes = static_cast<std::uint32_t>(bytes + kEnvelopeBytes);
        if (offset + recordBytes > kPayloadPartBytes) {
            ++part;
            offset = 0;
        }
        const auto current = *this;
        offset += recordBytes;
        ++ordinal;
        return current;
    }
};
constexpr std::string_view kIndexedMagic = "SATPAK03";
std::string bytesOf(std::span<const unsigned char> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
bool decryptSet(const std::filesystem::path& base, const PakKey& key, std::string& clear, std::string& error) {
    std::string setId;
    std::uint32_t count = 1;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto path = PakArchive::partPath(base, index);
        const auto fail = [&](const char* reason) { error = std::string(reason) + ": " + path.string(); return false; };
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) { return fail("Missing pak part"); }
        const auto length = file.tellg();
        if (length < static_cast<std::streamoff>(kEnvelopeBytes) || static_cast<std::uint64_t>(length) > kMaxBytes) { return fail("Invalid encrypted pak size"); }
        file.seekg(0);
        std::string header(kHeaderBytes, '\0'); file.read(header.data(), header.size());
        std::istringstream fields(header);
        char magic[8]{}; fields.read(magic, 8);
        std::string id(16, '\0'); fields.read(id.data(), 16);
        std::string nonce(12, '\0'); fields.read(nonce.data(), 12);
        const auto storedIndex = readInteger(fields, 4);
        const auto storedCount = readInteger(fields, 4);
        const auto size = readInteger(fields, 8);
        if (std::string_view(magic, 8) != "SATPAK02" || storedIndex != index || storedCount == 0 || storedCount > 4096 ||
            size != static_cast<std::uint64_t>(length) - kEnvelopeBytes) { return fail("Invalid pak set header"); }
        if (index == 0) { setId = id; count = static_cast<std::uint32_t>(storedCount); }
        if (setId != id || count != storedCount) { return fail("Mismatched pak set"); }
        std::string encrypted(static_cast<std::size_t>(size), '\0');
        file.read(encrypted.data(), static_cast<std::streamsize>(size));
        std::array<unsigned char, 16> tag{}; file.read(reinterpret_cast<char*>(tag.data()), tag.size());
        std::string part;
        if (!file || !pakCrypt(false, key, nonce, header, encrypted, part, tag)) { return fail("Pak authentication failed (wrong key or damaged data)"); }
        if (part.size() > clear.max_size() - clear.size()) { return fail("Pak set too large"); }
        clear.append(part);
    }
    return true;
}
bool indexedFormat(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    char magic[8]{};
    file.read(magic, sizeof(magic));
    return std::string_view(magic, sizeof(magic)) == kIndexedMagic;
}

bool readIndexedRecord(const std::filesystem::path& path, std::uint32_t offset, std::uint32_t ordinal,
    std::uint64_t limit, const PakKey& key, std::string& id, std::uint32_t& count, std::string& clear, std::string& error) {
    const auto fail = [&](const char* reason) { error = std::string(reason) + ": " + path.string(); return false; };
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { return fail("Missing pak part"); }
    const auto length = file.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) < offset + kEnvelopeBytes) {
        return fail("Truncated indexed pak record");
    }
    const auto available = static_cast<std::uint64_t>(length) - offset - kEnvelopeBytes;
    file.seekg(offset);
    std::string header(kHeaderBytes, '\0');
    file.read(header.data(), header.size());
    std::istringstream fields(header);
    fields.seekg(36);
    const auto index = readInteger(fields, 4);
    const auto records = readInteger(fields, 4);
    const auto bytes = readInteger(fields, 8);
    if (!file || !fields || !header.starts_with(kIndexedMagic) || index != ordinal ||
        records == 0 || records > kRecordLimit || ordinal >= records || bytes > available || bytes > limit ||
        (ordinal == 0 ? bytes != available : (bytes != limit || records != count || header.compare(8, 16, id) != 0))) {
        return fail("Invalid indexed pak header");
    }
    std::string cipher(static_cast<std::size_t>(bytes), '\0');
    std::array<unsigned char, 16> tag{};
    file.read(cipher.data(), static_cast<std::streamsize>(bytes));
    file.read(reinterpret_cast<char*>(tag.data()), tag.size());
    if (!file || !pakCrypt(false, key, std::string_view(header).substr(24, 12), header, cipher, clear, tag)) {
        return fail("Pak authentication failed (wrong key or damaged data)");
    }
    if (ordinal == 0) {
        id = header.substr(8, 16);
        count = static_cast<std::uint32_t>(records);
    }
    return true;
}

bool writeIndexedRecord(std::ostream& file, std::uint32_t ordinal, std::uint32_t count,
    std::string_view id, std::string_view clear, const PakKey& key, std::string& error) {
    std::array<unsigned char, 12> nonce{};
    if (!pakRandom(nonce)) { error = "Cannot generate pak nonce"; return false; }
    std::ostringstream fields;
    fields << kIndexedMagic << id << bytesOf(nonce);
    writeInteger(fields, ordinal, 4);
    writeInteger(fields, count, 4);
    writeInteger(fields, clear.size(), 8);
    const auto header = fields.str();
    std::string cipher;
    std::array<unsigned char, 16> tag{};
    if (!pakCrypt(true, key, bytesOf(nonce), header, clear, cipher, tag)) { error = "Pak encryption failed"; return false; }
    file.write(header.data(), header.size());
    file.write(cipher.data(), static_cast<std::streamsize>(cipher.size()));
    file.write(reinterpret_cast<const char*>(tag.data()), tag.size());
    if (!file) { error = "Cannot write indexed pak record"; return false; }
    return true;
}

bool publishSet(const std::filesystem::path& base, std::span<const std::filesystem::path> staged, std::string& error) {
    const auto count = staged.size();
    for (std::size_t i = count; i > 0; --i) {
        const auto destination = PakArchive::partPath(base, static_cast<std::uint32_t>(i - 1));
        std::error_code ec;
        std::filesystem::remove(destination, ec);
        ec.clear();
        std::filesystem::rename(staged[i - 1], destination, ec);
        if (ec) { error = "Cannot publish pak: " + ec.message(); return false; }
    }
    auto manifestPath = base;
    manifestPath += ".parts";
    std::ofstream manifest(manifestPath, std::ios::binary | std::ios::trunc);
    for (std::size_t i = 0; i < count; ++i) {
        const auto name = PakArchive::partPath(base, static_cast<std::uint32_t>(i)).filename().generic_u8string();
        manifest.write(reinterpret_cast<const char*>(name.data()), static_cast<std::streamsize>(name.size()));
        manifest.put('\n');
    }
    manifest.close();
    if (!manifest) { error = "Cannot write pak parts manifest"; return false; }
    return true;
}

bool encryptSet(const std::filesystem::path& base, std::string_view clear, const PakKey& key, std::uint64_t limit, std::string& error) {
    if (limit <= kEnvelopeBytes || limit > kMaxBytes) { error = "Invalid pak part size limit"; return false; }
    const auto capacity = static_cast<std::size_t>(limit - kEnvelopeBytes);
    const auto count = (clear.size() + capacity - 1) / capacity;
    if (count == 0 || count > 4096) { error = "Pak set exceeds 4096 parts"; return false; }
    std::array<unsigned char, 16> id{};
    if (!pakRandom(id)) { error = "Cannot generate pak set ID"; return false; }
    std::vector<std::filesystem::path> staged;
    for (std::size_t index = 0; index < count; ++index) {
        std::array<unsigned char, 12> nonce{};
        if (!pakRandom(nonce)) { error = "Cannot generate pak nonce"; return false; }
        const auto part = clear.substr(index * capacity, capacity);
        std::ostringstream fields;
        fields.write("SATPAK02", 8); fields << bytesOf(id) << bytesOf(nonce);
        writeInteger(fields, index, 4); writeInteger(fields, count, 4); writeInteger(fields, part.size(), 8);
        const auto header = fields.str();
        std::string encrypted;
        std::array<unsigned char, 16> tag{};
        if (!pakCrypt(true, key, bytesOf(nonce), header, part, encrypted, tag)) { error = "Pak encryption failed"; return false; }
        auto path = PakArchive::partPath(base, static_cast<std::uint32_t>(index)); path += ".tmp";
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(header.data(), header.size()); file.write(encrypted.data(), static_cast<std::streamsize>(encrypted.size()));
        file.write(reinterpret_cast<const char*>(tag.data()), tag.size()); file.close();
        if (!file) { error = "Cannot write encrypted pak: " + path.string(); return false; }
        staged.push_back(path);
    }
    // Publish base last. Interrupted replacement fails authentication rather than
    // silently combining generations. Stale extra parts are ignored by the loader.
    for (std::size_t i = count; i > 0; --i) {
        const auto destination = PakArchive::partPath(base, static_cast<std::uint32_t>(i - 1));
        std::error_code ec;
        std::filesystem::remove(destination, ec); ec.clear();
        std::filesystem::rename(staged[i - 1], destination, ec);
        if (ec) { error = "Cannot publish pak: " + ec.message(); return false; }
    }
    auto manifestPath = base; manifestPath += ".parts";
    std::ofstream manifest(manifestPath, std::ios::binary | std::ios::trunc);
    for (std::size_t i = 0; i < count; ++i) {
        const auto encoded = PakArchive::partPath(base, static_cast<std::uint32_t>(i)).filename().generic_u8string();
        manifest.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size())); manifest.put('\n');
    }
    manifest.close();
    if (!manifest) { error = "Cannot write pak parts manifest"; return false; }
    return true;
}
}
bool PakArchive::load(const std::filesystem::path& path, std::string& error, const PakKey& key) {
    error.clear();
    if (indexedFormat(path)) {
        PakReader reader;
        if (!reader.open(path, error, key)) { return false; }
        std::vector<PakEntry> candidate;
        candidate.reserve(reader.names().size());
        for (const auto& name : reader.names()) {
            PakEntry entry{name, {}};
            if (reader.read(name, entry.bytes, error, entry.bytes.max_size()) != PakReadResult::Found) { return false; }
            candidate.push_back(std::move(entry));
        }
        m_entries = std::move(candidate);
        return true;
    }
    return loadLegacy(path, error, key);
}
bool PakArchive::loadLegacy(const std::filesystem::path& path, std::string& error, const PakKey& key) {
    const auto fail = [&](const char* reason) { error = std::string(reason) + ": " + path.string(); return false; };
    std::string clear;
    if (!decryptSet(path, key, clear, error)) { return false; }
    std::istringstream file(std::move(clear));
    file.seekg(0, std::ios::end);
    const auto length = file.tellg();
    if (length < 12) { return fail("Invalid pak payload"); }
    std::uint64_t remaining = static_cast<std::uint64_t>(length) - 12;
    file.seekg(0);
    char magic[8]{};
    file.read(magic, 8);
    if (std::string_view(magic, 8) != kMagic) { return fail("Unsupported pak format"); }
    const auto count = readInteger(file, 4);
    if (count > remaining / 13) { return fail("Invalid pak entry count"); }
    std::vector<PakEntry> candidate;
    candidate.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        if (remaining < 12) { return fail("Truncated pak directory"); }
        remaining -= 12;
        const auto nameSize = readInteger(file, 4);
        const auto dataSize = readInteger(file, 8);
        if (!file || nameSize > 4096 || nameSize > remaining || dataSize > remaining - nameSize) {
            return fail("Invalid pak entry size");
        }
        PakEntry entry;
        entry.name.resize(static_cast<std::size_t>(nameSize));
        entry.bytes.resize(static_cast<std::size_t>(dataSize));
        file.read(entry.name.data(), static_cast<std::streamsize>(nameSize));
        file.read(entry.bytes.data(), static_cast<std::streamsize>(dataSize));
        if (!file || !validPath(entry.name)) { return fail("Invalid pak entry"); }
        remaining -= nameSize + dataSize;
        candidate.push_back(std::move(entry));
    }
    if (remaining != 0) { return fail("Trailing pak data"); }
    std::sort(candidate.begin(), candidate.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    for (std::size_t i = 1; i < candidate.size(); ++i) {
        if (candidate[i - 1].name == candidate[i].name) { return fail("Duplicate pak path"); }
    }
    m_entries = std::move(candidate);
    return true;
}
std::string_view PakArchive::find(std::string_view name) const {
    const auto found = std::lower_bound(m_entries.begin(), m_entries.end(), name,
        [](const PakEntry& entry, std::string_view key) { return entry.name < key; });
    return found != m_entries.end() && found->name == name ? std::string_view(found->bytes) : std::string_view{};
}
bool PakArchive::write(const std::filesystem::path& path, std::span<const PakEntry> entries, std::string& error, const PakKey& key, std::uint64_t maxPartBytes) {
    error.clear();
    if (entries.size() > std::numeric_limits<std::uint32_t>::max()) { error = "Too many pak entries"; return false; }
    std::uint64_t total = 12;
    std::vector<std::string_view> names;
    for (const auto& entry : entries) {
        if (!validPath(entry.name)) { error = "Invalid pak entry: " + entry.name; return false; }
        total += 12 + entry.name.size() + entry.bytes.size();
        if (total > std::numeric_limits<std::size_t>::max()) { error = "Pak exceeds address space"; return false; }
        names.push_back(entry.name);
    }
    std::sort(names.begin(), names.end());
    if (std::adjacent_find(names.begin(), names.end()) != names.end()) { error = "Duplicate pak path"; return false; }
    std::ostringstream file;
    file.write(kMagic.data(), 8);
    writeInteger(file, entries.size(), 4);
    for (const auto& entry : entries) {
        writeInteger(file, entry.name.size(), 4);
        writeInteger(file, entry.bytes.size(), 8);
        file.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
        file.write(entry.bytes.data(), static_cast<std::streamsize>(entry.bytes.size()));
    }
    if (!file) { error = "Cannot serialize pak"; return false; }
    return encryptSet(path, std::move(file).str(), key, maxPartBytes, error);
}
std::filesystem::path PakArchive::partPath(const std::filesystem::path& base, std::uint32_t index) {
    if (index == 0) { return base; }
    auto name = base.stem(); name += "." + std::to_string(index); name += base.extension();
    return base.parent_path() / name;
}
struct PakReader::Impl {
    struct Entry { ChunkCursor start; std::uint64_t size; };
    std::filesystem::path path;
    ScopedPakKey key;
    std::string setId;
    std::uint32_t recordCount = 0;
    std::vector<std::string> names;
    std::vector<Entry> entries;
    PakArchive legacy;
    mutable std::mutex cacheMutex;
    mutable std::uint32_t cachedRecord = 0;
    mutable std::string cachedBytes;
};

PakReader::PakReader() : m_impl(std::make_unique<Impl>()) {}
PakReader::~PakReader() = default;

bool PakReader::open(const std::filesystem::path& path, std::string& error, const PakKey& key) {
    error.clear();
    auto candidate = std::make_unique<Impl>();
    if (!indexedFormat(path)) {
        if (!candidate->legacy.loadLegacy(path, error, key)) { return false; }
        for (const auto& entry : candidate->legacy.entries()) { candidate->names.push_back(entry.name); }
        m_impl = std::move(candidate);
        return true;
    }
    std::string index;
    if (!readIndexedRecord(path, 0, 0, kIndexLimit, key, candidate->setId, candidate->recordCount, index, error)) { return false; }
    const auto badIndex = [&] { error = "Invalid indexed pak directory: " + path.string(); return false; };
    if (index.size() < 4) { return badIndex(); }
    std::uint64_t remaining = index.size() - 4;
    std::istringstream directory(std::move(index));
    const auto count = readInteger(directory, 4);
    if (count > remaining / 13) { return badIndex(); }
    candidate->names.reserve(static_cast<std::size_t>(count));
    candidate->entries.reserve(static_cast<std::size_t>(count));
    ChunkCursor cursor;
    for (std::uint64_t i = 0; i < count; ++i) {
        if (remaining < 12) { return badIndex(); }
        remaining -= 12;
        const auto length = readInteger(directory, 4);
        const auto bytes = readInteger(directory, 8);
        if (length == 0 || length > 4096 || length > remaining ||
            bytes > kPayloadCapacity) { return badIndex(); }
        std::string name(static_cast<std::size_t>(length), '\0');
        directory.read(name.data(), static_cast<std::streamsize>(length));
        if (!directory || !validPath(name) || (!candidate->names.empty() && candidate->names.back() >= name)) {
            return badIndex();
        }
        remaining -= length;
        candidate->names.push_back(std::move(name));
        candidate->entries.push_back({cursor, bytes});
        for (auto left = bytes; left != 0;) {
            const auto chunkSize = std::min(left, kChunkBytes);
            cursor.advance(chunkSize);
            if (cursor.part >= kPartLimit || cursor.ordinal > candidate->recordCount) { return badIndex(); }
            left -= chunkSize;
        }
    }
    if (remaining != 0 || cursor.ordinal != candidate->recordCount) {
        return badIndex();
    }
    candidate->key.bytes = key;
    candidate->path = path;
    m_impl = std::move(candidate);
    return true;
}

std::span<const std::string> PakReader::names() const { return m_impl->names; }

PakReadResult PakReader::read(std::string_view name, std::string& bytes, std::string& error, std::size_t limit) const {
    error.clear();
    const auto& pak = *m_impl;
    const auto found = std::lower_bound(pak.names.begin(), pak.names.end(), name);
    if (found == pak.names.end() || *found != name) { return PakReadResult::Missing; }
    const auto tooLarge = [&] { error = "Packed entry exceeds read limit: " + std::string(name); return PakReadResult::Error; };
    if (pak.recordCount == 0) {
        const auto data = pak.legacy.find(name);
        if (data.size() > limit) { return tooLarge(); }
        bytes.assign(data);
        return PakReadResult::Found;
    }
    const auto entry = pak.entries[static_cast<std::size_t>(found - pak.names.begin())];
    if (entry.size > limit || entry.size > bytes.max_size()) { return tooLarge(); }
    std::string result;
    result.reserve(static_cast<std::size_t>(entry.size));
    std::lock_guard lock(pak.cacheMutex);
    auto cursor = entry.start;
    for (auto left = entry.size; left != 0;) {
        const auto size = std::min(left, kChunkBytes);
        const auto location = cursor.advance(size);
        if (location.ordinal != pak.cachedRecord) {
            auto id = pak.setId;
            auto count = pak.recordCount;
            std::string chunk;
            const auto part = PakArchive::partPath(pak.path, location.part);
            if (!readIndexedRecord(part, location.offset, location.ordinal, size, pak.key.bytes, id, count, chunk, error)) {
                return PakReadResult::Error;
            }
            pak.cachedRecord = location.ordinal;
            pak.cachedBytes = std::move(chunk);
        }
        result.append(pak.cachedBytes);
        left -= size;
    }
    bytes = std::move(result);
    return PakReadResult::Found;
}

bool PakReader::write(const std::filesystem::path& path, std::span<const PakEntry> entries,
    std::string& error, const PakKey& key) {
    error.clear();
    if (entries.size() > kIndexLimit / 13) { error = "Too many pak entries"; return false; }
    std::vector<const PakEntry*> ordered;
    ordered.reserve(entries.size());
    std::uint64_t payloadBytes = 0;
    std::uint64_t indexBytes = 4;
    for (const auto& entry : entries) {
        if (!validPath(entry.name)) { error = "Invalid pak entry: " + entry.name; return false; }
        indexBytes += 12 + entry.name.size();
        if (indexBytes > kIndexLimit || entry.bytes.size() > kPayloadCapacity - payloadBytes) {
            error = "Indexed pak exceeds size limit";
            return false;
        }
        payloadBytes += entry.bytes.size();
        ordered.push_back(&entry);
    }
    std::ranges::sort(ordered, {}, &PakEntry::name);
    std::ostringstream directory;
    writeInteger(directory, entries.size(), 4);
    ChunkCursor layout;
    std::string_view previous;
    for (const auto* entry : ordered) {
        if (entry->name == previous) { error = "Duplicate pak path: " + entry->name; return false; }
        previous = entry->name;
        writeInteger(directory, entry->name.size(), 4);
        writeInteger(directory, entry->bytes.size(), 8);
        directory << entry->name;
        for (std::uint64_t left = entry->bytes.size(); left != 0;) {
            const auto size = std::min(left, kChunkBytes);
            layout.advance(size);
            left -= size;
        }
        if (layout.part >= kPartLimit) { error = "Indexed pak exceeds part limit"; return false; }
    }
    std::array<unsigned char, 16> id{};
    if (!pakRandom(id)) { error = "Cannot generate pak set ID"; return false; }
    const auto count = layout.ordinal;
    struct Staging {
        std::vector<std::filesystem::path> paths;
        ~Staging() {
            for (const auto& path : paths) {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
        }
    } staged;
    std::ofstream file;
    const auto openPart = [&](std::uint32_t part) {
        if (file.is_open()) { file.close(); }
        auto temporary = PakArchive::partPath(path, part);
        temporary += ".tmp";
        staged.paths.push_back(std::move(temporary));
        if (file) { file.open(staged.paths.back(), std::ios::binary | std::ios::trunc); }
        if (!file) { error = "Cannot write indexed pak part: " + staged.paths.back().string(); return false; }
        return true;
    };
    if (!openPart(0) || !writeIndexedRecord(file, 0, count, bytesOf(id), std::move(directory).str(), key, error)) {
        return false;
    }
    ChunkCursor cursor;
    std::uint32_t activePart = 0;
    for (const auto* entry : ordered) {
        std::string_view bytes = entry->bytes;
        while (!bytes.empty()) {
            const auto size = std::min(bytes.size(), static_cast<std::size_t>(kChunkBytes));
            const auto location = cursor.advance(size);
            if (activePart != location.part) {
                if (!openPart(location.part)) { return false; }
                activePart = location.part;
            }
            if (!writeIndexedRecord(file, location.ordinal, count, bytesOf(id), bytes.substr(0, size), key, error)) {
                return false;
            }
            bytes.remove_prefix(size);
        }
    }
    file.close();
    if (!file) { error = "Cannot close indexed pak part"; return false; }
    return publishSet(path, staged.paths, error);
}
} // namespace sat
