#include "UiFileInterface.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace sat {
namespace {

const AssetReader* currentReader = nullptr;
UiFileInterface::StyleSheets* currentStyleSheets = nullptr;

struct OpenFile {
    std::string bytes;
    std::size_t position = 0;
};

} // namespace

UiFileInterface::Scope::Scope(const AssetReader& reader, StyleSheets* styleSheets)
    : m_previous(currentReader), m_previousStyleSheets(currentStyleSheets) {
    currentReader = &reader;
    currentStyleSheets = styleSheets;
}

UiFileInterface::Scope::~Scope() {
    currentReader = m_previous;
    currentStyleSheets = m_previousStyleSheets;
}

bool UiFileInterface::LoadFile(const Rml::String& path, Rml::String& bytes) {
    if (!currentReader || !*currentReader || !path.starts_with("Assets/UI/") || !isValidAssetPath(path)) {
        return false;
    }
    std::string error;
    const bool found = (*currentReader)(path, bytes, error) == AssetReadResult::Found;
    if (currentStyleSheets && path.ends_with(".rcss")) {
        currentStyleSheets->insert_or_assign(path, found ? std::optional{bytes} : std::nullopt);
    }
    return found;
}

Rml::FileHandle UiFileInterface::Open(const Rml::String& path) {
    auto file = std::make_unique<OpenFile>();
    return LoadFile(path, file->bytes) ? reinterpret_cast<Rml::FileHandle>(file.release()) : 0;
}

void UiFileInterface::Close(Rml::FileHandle file) {
    delete reinterpret_cast<OpenFile*>(file);
}

std::size_t UiFileInterface::Read(void* buffer, std::size_t size, Rml::FileHandle file) {
    auto& open = *reinterpret_cast<OpenFile*>(file);
    const auto count = std::min(size, open.bytes.size() - open.position);
    if (count > 0) { std::memcpy(buffer, open.bytes.data() + open.position, count); }
    open.position += count;
    return count;
}

bool UiFileInterface::Seek(Rml::FileHandle file, long offset, int origin) {
    auto& open = *reinterpret_cast<OpenFile*>(file);
    const auto length = open.bytes.size();
    std::size_t base = 0;
    switch (origin) {
    case SEEK_SET: break;
    case SEEK_CUR: base = open.position; break;
    case SEEK_END: base = length; break;
    default: return false;
    }
    if (offset < 0) {
        const auto distance = static_cast<unsigned long>(-(offset + 1)) + 1;
        if (distance > base) { return false; }
        open.position = base - distance;
    } else {
        const auto distance = static_cast<unsigned long>(offset);
        if (distance > length - base) { return false; }
        open.position = base + distance;
    }
    return true;
}

std::size_t UiFileInterface::Tell(Rml::FileHandle file) {
    return reinterpret_cast<OpenFile*>(file)->position;
}

std::size_t UiFileInterface::Length(Rml::FileHandle file) {
    return reinterpret_cast<OpenFile*>(file)->bytes.size();
}

} // namespace sat
