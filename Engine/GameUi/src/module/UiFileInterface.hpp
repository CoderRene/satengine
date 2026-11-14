#pragma once

#include "AssetLibrary.hpp"

#include <RmlUi/Core/FileInterface.h>

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

namespace sat {

class UiFileInterface final : public Rml::FileInterface {
public:
    using StyleSheets = std::unordered_map<std::string, std::optional<std::string>>;

    // RmlUi installs one file interface globally. Select the current reader for
    // synchronous work on a context, including texture creation during rendering.
    class Scope {
    public:
        explicit Scope(const AssetReader& reader, StyleSheets* styleSheets = nullptr);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        const AssetReader* m_previous;
        StyleSheets* m_previousStyleSheets;
    };

    Rml::FileHandle Open(const Rml::String& path) override;
    void Close(Rml::FileHandle file) override;
    std::size_t Read(void* buffer, std::size_t size, Rml::FileHandle file) override;
    bool Seek(Rml::FileHandle file, long offset, int origin) override;
    std::size_t Tell(Rml::FileHandle file) override;
    std::size_t Length(Rml::FileHandle file) override;
    bool LoadFile(const Rml::String& path, Rml::String& bytes) override;
};

} // namespace sat
