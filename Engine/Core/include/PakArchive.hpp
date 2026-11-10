#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sat {
struct PakEntry {
    std::string name;
    std::string bytes;
};
using PakKey = std::array<unsigned char, 32>;
void clearPakKey(PakKey& key);
// Clears temporary key material on success, failure, and exception unwinding.
struct ScopedPakKey {
    PakKey bytes{};
    ScopedPakKey() = default;
    ~ScopedPakKey() { clearPakKey(bytes); }
    ScopedPakKey(const ScopedPakKey&) = delete;
    ScopedPakKey& operator=(const ScopedPakKey&) = delete;
    ScopedPakKey(ScopedPakKey&&) = delete;
    ScopedPakKey& operator=(ScopedPakKey&&) = delete;
};
// Owns decrypted bytes; views expire on load/destruction. Window-thread only.
// Encrypted split set v2; base.pak followed by base.1.pak, base.2.pak, ... .
// AES-256-GCM authenticates the set ID, sequence metadata and encrypted payload.
class PakArchive {
public:
    [[nodiscard]] bool load(const std::filesystem::path& path, std::string& error, const PakKey& key);
    [[nodiscard]] std::string_view find(std::string_view name) const;
    [[nodiscard]] std::span<const PakEntry> entries() const { return m_entries; }
    [[nodiscard]] static bool write(const std::filesystem::path& path,
                                   std::span<const PakEntry> entries, std::string& error, const PakKey& key,
                                   std::uint64_t maxPartBytes = 2ULL * 1024 * 1024 * 1024);
    [[nodiscard]] static std::filesystem::path partPath(const std::filesystem::path& base, std::uint32_t index);
private:
    std::vector<PakEntry> m_entries;
};
} // namespace sat
