#include "SceneAnimationFile.hpp"
#include "AnimationAsset.hpp"
#include "AnimationGraph.hpp"
#include "Scene.hpp"
#include <cmath>
#include <iomanip>
#include <istream>
#include <ostream>

namespace sat {
void writeAnimationComponent(std::ostream& out, const Entity& entity) {
    const auto& animator = entity.animator;
    out << entity.hasAnimator << ' ' << std::quoted(animator.clip) << ' ' << std::quoted(animator.controllerPath)
        << ' ' << animator.playOnStart << ' ' << animator.loop << ' ' << animator.speed << ' ' << animator.crossfadeSeconds << '\n';
}
bool readAnimationComponent(std::istream& in, Entity& entity, std::string& error) {
    auto& animator = entity.animator;
    if (!(in >> entity.hasAnimator >> std::quoted(animator.clip) >> std::quoted(animator.controllerPath)
        >> animator.playOnStart >> animator.loop >> animator.speed >> animator.crossfadeSeconds) ||
        animator.clip.size() > 4096 || !std::isfinite(animator.speed) || !std::isfinite(animator.crossfadeSeconds) || animator.crossfadeSeconds < 0 ||
        (!animator.controllerPath.empty() && !isAnimationGraphAssetPath(animator.controllerPath)) ||
        (entity.hasAnimator && (!entity.hasMesh || entity.mesh.meshNode || !isGltfAssetPath(entity.mesh.meshPath)))) {
        error = "Invalid Animator component or mesh"; return false;
    }
    return true;
}
}
