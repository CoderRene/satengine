#include "PakArchive.hpp"
#include "module/Pak/PakCrypto.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <limits>
#include <utility>

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
} // namespace sat
