#include "Renderer.hpp"

#include "module/RenderSession.hpp"

#include <utility>

namespace sat {

struct Renderer::Impl : RenderSession {
    using RenderSession::RenderSession;
};

Renderer::Renderer(void* glfwWindow)
    : m_impl(std::make_unique<Impl>(glfwWindow)) {}

Renderer::~Renderer() = default;

bool Renderer::valid() const {
    return m_impl->valid();
}

void Renderer::invalidateMaterial(std::string_view path) {
    m_impl->invalidateMaterial(path);
}

void Renderer::setShaderReader(AssetReader reader) {
    m_impl->setShaderReader(std::move(reader));
}

bool Renderer::initializeShaders() {
    return m_impl->initializeShaders();
}

void Renderer::setAssetReader(AssetReader reader) {
    m_impl->setAssetReader(std::move(reader));
}

void Renderer::setProjectShaderSource(AssetReader reader) {
    m_impl->setProjectShaderSource(std::move(reader));
}

bool Renderer::reimportAsset(std::string_view path, const AssetImportSettings* settings,
    const AssetImportWriter& writer, std::string& error) {
    return m_impl->reimportAsset(path, settings, writer, error);
}

std::string Renderer::assetInfo(std::string_view path) {
    return m_impl->assetInfo(path);
}

const std::vector<std::string>& Renderer::assetErrors() const {
    return m_impl->assetErrors();
}

bool Renderer::meshBounds(std::string_view path, Vec3& minimum, Vec3& maximum) {
    return m_impl->meshBounds(path, minimum, maximum);
}

void Renderer::newFrame() {
    m_impl->newFrame();
}

void Renderer::resizeGameView(int width, int height) {
    m_impl->resizeGameView(width, height);
}

void Renderer::resizeSceneView(int width, int height) {
    m_impl->resizeSceneView(width, height);
}

void Renderer::renderGameView(const GameViewCamera& camera, const SceneDrawList& drawList) {
    m_impl->renderGameView(camera, drawList);
}

void Renderer::renderSceneView(const GameViewCamera& camera, const SceneDrawList& drawList) {
    m_impl->renderSceneView(camera, drawList);
}

std::uintptr_t Renderer::gameColorTexture() const {
    return m_impl->gameColorTexture();
}

std::uintptr_t Renderer::sceneColorTexture() const {
    return m_impl->sceneColorTexture();
}

int Renderer::gameViewWidth() const {
    return m_impl->gameViewWidth();
}

int Renderer::gameViewHeight() const {
    return m_impl->gameViewHeight();
}

std::uint64_t Renderer::contentRevision() const {
    return m_impl->contentRevision();
}

std::uintptr_t Renderer::materialThumbnail(std::string_view path) {
    return m_impl->materialThumbnail(path);
}

std::uintptr_t Renderer::createTexture(const unsigned char* rgba, int width, int height) {
    return m_impl->createTexture(rgba, width, height);
}

void Renderer::destroyTexture(std::uintptr_t texture) {
    m_impl->destroyTexture(texture);
}

bool Renderer::renderWindow(const GameViewCamera& camera, const SceneDrawList& drawList) {
    return m_impl->renderWindow(camera, drawList);
}

void Renderer::present() {
    m_impl->present();
}

void* Renderer::nativeDevice() const {
    return m_impl->nativeDevice();
}

void* Renderer::nativeContext() const {
    return m_impl->nativeContext();
}

void Renderer::bindWindowTarget() {
    m_impl->bindWindowTarget();
}

} // namespace sat
