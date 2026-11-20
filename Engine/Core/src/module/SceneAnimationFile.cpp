#include "SceneAnimationFile.hpp"
#include "AnimationAsset.hpp"
#include "AnimationGraph.hpp"
#include "Scene.hpp"
#include <cmath>
#include <iomanip>
#include <istream>
#include <ostream>

namespace sat {
void writeAnimationComponent(std::ostream& out, const Entity& entity, bool hasOcclusionRadius) {
    const auto& animator = entity.animator;
    out << entity.hasAnimator << ' ' << std::quoted(animator.clip) << ' ' << std::quoted(animator.controllerPath)
        << ' ' << animator.playOnStart << ' ' << animator.loop << ' ' << animator.speed << ' ' << animator.crossfadeSeconds
        << ' ' << static_cast<int>(animator.rotationBlend) << ' ' << static_cast<int>(animator.cullingMode);
    if (hasOcclusionRadius) { out << ' ' << animator.occlusionRadius; }
    out << '\n';
}
bool readAnimationComponent(std::istream& in, Entity& entity, bool hasModes, bool hasOcclusionRadius, std::string& error) {
    auto& animator = entity.animator;
    if (!(in >> entity.hasAnimator >> std::quoted(animator.clip) >> std::quoted(animator.controllerPath)
        >> animator.playOnStart >> animator.loop >> animator.speed >> animator.crossfadeSeconds) ||
        animator.clip.size() > 4096 || !std::isfinite(animator.speed) || !std::isfinite(animator.crossfadeSeconds) || animator.crossfadeSeconds < 0 ||
        (!animator.controllerPath.empty() && !isAnimationGraphAssetPath(animator.controllerPath)) ||
        (entity.hasAnimator && entity.hasMesh && !isSceneModelAssetPath(entity.mesh.meshPath))) {
        error = "Invalid Animator component or mesh"; return false;
    }
    if (hasModes) {
        int rotationBlend = 0, cullingMode = 0;
        if (!(in >> rotationBlend >> cullingMode) ||
            rotationBlend < 0 || rotationBlend > static_cast<int>(AnimationRotationBlend::Nlerp) ||
            cullingMode < 0 || cullingMode > static_cast<int>(AnimationCullingMode::Automatic)) {
            error = "Invalid Animator interpolation or culling mode"; return false;
        }
        animator.rotationBlend = static_cast<AnimationRotationBlend>(rotationBlend);
        animator.cullingMode = static_cast<AnimationCullingMode>(cullingMode);
    }
    if (hasOcclusionRadius && (!(in >> animator.occlusionRadius) ||
            !std::isfinite(animator.occlusionRadius) || animator.occlusionRadius < 0)) {
        error = "Invalid Animator occlusion radius"; return false;
    }
    return true;
}
}
