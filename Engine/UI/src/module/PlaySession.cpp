#include "PlaySession.hpp"

#include "GameUi.hpp"

#include "Input.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace sat {
namespace {

template <typename T>
void applyEdit(const T& before, const T& after, T& live) {
    if (before != after) {
        live = after;
    }
}

template <typename Vector>
void applyVectorEdit(const Vector& before, const Vector& after, Vector& live) {
    applyEdit(before.x, after.x, live.x);
    applyEdit(before.y, after.y, live.y);
    applyEdit(before.z, after.z, live.z);
}

void applyEntityEdits(const Entity& before, const Entity& after, Entity& live, LuaRuntime& scripts) {
    applyEdit(before.name, after.name, live.name);
    applyVectorEdit(before.transform.position, after.transform.position, live.transform.position);
    applyVectorEdit(before.transform.rotation, after.transform.rotation, live.transform.rotation);
    applyVectorEdit(before.transform.scale, after.transform.scale, live.transform.scale);
    applyEdit(before.hasCamera, after.hasCamera, live.hasCamera);
    applyEdit(before.camera.fieldOfView, after.camera.fieldOfView, live.camera.fieldOfView);
    applyEdit(before.camera.nearClip, after.camera.nearClip, live.camera.nearClip);
    applyEdit(before.camera.farClip, after.camera.farClip, live.camera.farClip);
    for (int channel = 0; channel < 4; ++channel) {
        applyEdit(before.camera.clearColor[channel], after.camera.clearColor[channel], live.camera.clearColor[channel]);
    }
    applyEdit(before.camera.isMain, after.camera.isMain, live.camera.isMain);
    applyEdit(before.hasMesh, after.hasMesh, live.hasMesh);
    applyEdit(before.mesh.meshPath, after.mesh.meshPath, live.mesh.meshPath);
    applyEdit(before.mesh.materialPath, after.mesh.materialPath, live.mesh.materialPath);
    applyEdit(before.mesh.primitive, after.mesh.primitive, live.mesh.primitive);
    applyEdit(before.hasLight, after.hasLight, live.hasLight);
    if (before.hasLight != after.hasLight) { live.light = after.light; }
    applyEdit(before.light.type, after.light.type, live.light.type);
    for (int channel = 0; channel < 3; ++channel) {
        applyEdit(before.light.color[channel], after.light.color[channel], live.light.color[channel]);
    }
    applyEdit(before.light.intensity, after.light.intensity, live.light.intensity);
    applyEdit(before.light.range, after.light.range, live.light.range);
    if (before.light.innerConeDegrees != after.light.innerConeDegrees ||
        before.light.outerConeDegrees != after.light.outerConeDegrees) {
        live.light.innerConeDegrees = after.light.innerConeDegrees;
        live.light.outerConeDegrees = after.light.outerConeDegrees;
    }
    if (before.scripts != after.scripts) {
        // Preserve runtime changes to script entries left alone by the editor.
        for (const auto& script : before.scripts) {
            const auto authored = std::ranges::find(after.scripts, script.path, &ScriptComponent::path);
            const auto runtime = std::ranges::find(live.scripts, script.path, &ScriptComponent::path);
            if (authored == after.scripts.end()) {
                if (runtime != live.scripts.end()) { live.scripts.erase(runtime); }
            } else if (runtime != live.scripts.end()) {
                applyEdit(script.enabled, authored->enabled, runtime->enabled);
                scripts.applyScriptFieldEdits(live.id(), script, *authored);
                applyEdit(script.fieldOverrides, authored->fieldOverrides, runtime->fieldOverrides);
            }
        }
        for (const auto& script : after.scripts) {
            if (std::ranges::find(before.scripts, script.path, &ScriptComponent::path) == before.scripts.end() &&
                std::ranges::find(live.scripts, script.path, &ScriptComponent::path) == live.scripts.end()) {
                live.scripts.push_back(script);
            }
        }
    }
    applyEdit(before.hasRigidBody, after.hasRigidBody, live.hasRigidBody);
    if (before.hasRigidBody != after.hasRigidBody || before.rigidBody != after.rigidBody) {
        live.rigidBody = after.rigidBody;
    }
    applyEdit(before.hasCollider, after.hasCollider, live.hasCollider);
    if (before.hasCollider != after.hasCollider || before.collider != after.collider) {
        live.collider = after.collider;
    }
    applyEdit(before.hasGravitySource, after.hasGravitySource, live.hasGravitySource);
    if (before.hasGravitySource != after.hasGravitySource || before.gravitySource != after.gravitySource) {
        live.gravitySource = after.gravitySource;
    }
    applyEdit(before.gameUi, after.gameUi, live.gameUi);
    applyEdit(before.hasAudioSource, after.hasAudioSource, live.hasAudioSource);
    if (before.hasAudioSource != after.hasAudioSource || before.audioSource != after.audioSource) {
        live.audioSource = after.audioSource;
    }
    applyEdit(before.hasAudioListener, after.hasAudioListener, live.hasAudioListener);
}

} // namespace

PlaySession::PlaySession(const Input& input)
    : m_physics(m_game), m_audio(m_game),
      m_scripts(m_game, &input, &m_log, &m_physics, &m_audio) {}

bool PlaySession::takeFocusGame() {
    const bool focus = m_focusGame;
    m_focusGame = false;
    return focus;
}

bool PlaySession::takeFocusLog() {
    const bool focus = m_focusLog;
    m_focusLog = false;
    return focus;
}

const Scene& PlaySession::viewScene(const Scene& authored) const {
    return m_active ? m_game : authored;
}

std::size_t PlaySession::physicsWorldCount() const {
    return m_physics.frameCount();
}

void PlaySession::setAssetReader(AssetReader reader) {
    m_audio.setAssetReader(std::move(reader));
}

bool PlaySession::update(float deltaSeconds) {
    if (!m_scripts.update(deltaSeconds)) { return false; }
    if (m_gameUi) { m_gameUi->update(); }
    return true;
}

void PlaySession::stop(const Scene& authored) {
    if (!m_active) {
        return;
    }
    m_scripts.stop();
    m_scripts.setGameUi(nullptr);
    if (m_gameUi) { m_gameUi->clear(); }
    m_gameUi = nullptr;
    if (!m_scripts.error().empty()) {
        m_log.error(m_scripts.error());
        m_focusLog = true;
    }
    m_game.clear();
    m_applied.reset();
    m_active = false;
    m_focusGame = false;
    if (authored.editRevision() == m_prePlay->editRevision()) {
        m_prePlay.reset();
    }
}

void PlaySession::discardInto(Scene& authored) {
    authored = std::move(*m_prePlay);
    m_prePlay.reset();
}

void PlaySession::acceptEdits() {
    m_prePlay.reset();
}

void PlaySession::defer(std::function<void()> action) {
    m_afterDecision = std::move(action);
}

std::function<void()> PlaySession::takePendingAction() {
    return std::exchange(m_afterDecision, {});
}

bool PlaySession::start(Scene& authored, const std::filesystem::path& projectRoot, GameUi* ui) {
    m_prePlay = authored;
    m_applied = authored;
    m_game = authored;
    m_log.clear();
    m_active = true;
    m_gameUi = ui;
    m_scripts.setGameUi(ui);
    if (!m_scripts.start(projectRoot)) {
        stop(authored);
        return false;
    }
    m_focusGame = true;
    return true;
}

void PlaySession::applyEdits(Scene& authored, std::string& error) {
    if (!m_active || authored.editRevision() == m_applied->editRevision()) { return; }

    if (authored.destructionRevision() != m_applied->destructionRevision()) {
        for (const Entity& before : m_applied->entities()) {
            if (!authored.entityById(before.id())) {
                (void)m_game.destroyEntity(before.id());
            }
        }
    }
    for (const Entity& after : authored.entities()) {
        const Entity* before = m_applied->entityById(after.id());
        if (!before) {
            m_game.addEntityCopy(after);
        } else if (before->parentId() != after.parentId() && m_game.entityById(after.id())) {
            // Remove changed edges before adding any new ones. A valid final
            // hierarchy can temporarily form a cycle when applied one edge at a time.
            (void)m_game.setParent(after.id(), kInvalidEntityId);
        }
    }
    for (const Entity& after : authored.entities()) {
        const Entity* before = m_applied->entityById(after.id());
        Entity* live = m_game.entityById(after.id());
        if (!live) {
            continue;
        }
        if ((!before || before->parentId() != after.parentId()) &&
            m_game.entityById(after.parentId()) &&
            !m_game.setParent(after.id(), after.parentId())) {
            error = "Cannot sync hierarchy for " + after.name;
        }
        if (before) {
            applyEntityEdits(*before, after, *live, m_scripts);
            if (before->hasAudioSource && !after.hasAudioSource) {
                (void)m_audio.stopSound(after.id());
            } else if (after.hasAudioSource) {
                if (before->audioSource.volume != after.audioSource.volume) {
                    (void)m_audio.setVolume(after.id(), after.audioSource.volume);
                }
                if (before->audioSource.pitch != after.audioSource.pitch) {
                    (void)m_audio.setPitch(after.id(), after.audioSource.pitch);
                }
            }
        }
        const auto& scale = after.transform.scale;
        const bool colliderResized = before && after.hasCollider && before->transform.scale != scale &&
            scale.x > 0 && scale.x == scale.y && scale.x == scale.z;
        const bool bodyChanged = before && (before->hasCollider != after.hasCollider ||
            before->hasRigidBody != after.hasRigidBody ||
            (after.hasCollider && (before->collider != after.collider || before->rigidBody != after.rigidBody)));
        if (m_physics.running() && (colliderResized || bodyChanged || (!before && after.hasCollider))) {
            if (!m_physics.refreshBody(after.id(),
                    !before || before->rigidBody.linearVelocity != after.rigidBody.linearVelocity,
                    !before || before->rigidBody.angularVelocity != after.rigidBody.angularVelocity)) {
                error = m_physics.error();
            }
        }
        if (after.hasCamera && after.camera.isMain &&
            (!before || !before->hasCamera || !before->camera.isMain)) {
            (void)m_game.setMainCameraById(after.id());
        }
    }
    if (m_audio.running()) {
        const auto& before = m_applied->audioSettings;
        const auto& after = authored.audioSettings;
        if (before.masterVolume != after.masterVolume) {
            m_audio.setMasterVolume(after.masterVolume);
        }
        for (std::size_t index = 0; index < kAudioBusCount; ++index) {
            if (before.busVolumes[index] != after.busVolumes[index]) {
                (void)m_audio.setBusVolume(static_cast<AudioBus>(index), after.busVolumes[index]);
            }
        }
    }
    const auto& ambientBefore = m_applied->lightingSettings;
    const auto& ambientAfter = authored.lightingSettings;
    auto& ambientLive = m_game.lightingSettings;
    for (int i = 0; i < 3; ++i) {
        applyEdit(ambientBefore.ambientColor[i], ambientAfter.ambientColor[i], ambientLive.ambientColor[i]);
    }
    applyEdit(ambientBefore.ambientIntensity, ambientAfter.ambientIntensity, ambientLive.ambientIntensity);
    applyEdit(ambientBefore.environmentPath, ambientAfter.environmentPath, ambientLive.environmentPath);
    applyEdit(ambientBefore.environmentIntensity, ambientAfter.environmentIntensity, ambientLive.environmentIntensity);
    applyEdit(ambientBefore.directionalShadows, ambientAfter.directionalShadows, ambientLive.directionalShadows);
    applyEdit(ambientBefore.shadowResolution, ambientAfter.shadowResolution, ambientLive.shadowResolution);
    applyEdit(ambientBefore.shadowDistance, ambientAfter.shadowDistance, ambientLive.shadowDistance);
    applyEdit(ambientBefore.shadowCascadeCount, ambientAfter.shadowCascadeCount, ambientLive.shadowCascadeCount);
    applyEdit(ambientBefore.shadowSplitLambda, ambientAfter.shadowSplitLambda, ambientLive.shadowSplitLambda);
    applyEdit(ambientBefore.bloom, ambientAfter.bloom, ambientLive.bloom);
    *m_applied = authored;
}

} // namespace sat
