#pragma once

#include "AssetData.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sat {
struct AnimationClipAsset {
    AnimationClip clip;
    std::vector<std::string> jointNames;
    float startSeconds = 0;
    float endSeconds = 0;
    bool loop = true;
    float speed = 1;
};
[[nodiscard]] bool isAnimationClipAssetPath(std::string_view path);
[[nodiscard]] bool validateAnimationClip(const AnimationClipAsset& asset, std::string& error);
[[nodiscard]] bool encodeAnimationClip(const AnimationClipAsset& asset, std::string& bytes, std::string& error);
[[nodiscard]] bool decodeAnimationClip(std::string_view bytes, AnimationClipAsset& asset, std::string& error);
// Missing names map to -1. The caller caches this mapping with the mesh and clip.
[[nodiscard]] std::vector<int> bindAnimationClip(const AnimationClipAsset& asset, const AnimationData& mesh);
} // namespace sat
