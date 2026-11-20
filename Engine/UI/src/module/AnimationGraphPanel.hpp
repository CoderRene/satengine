#pragma once
#include "AnimationGraph.hpp"
#include "Scene.hpp"
#include <string>
#include <string_view>
struct ImNodesContext;
namespace sat {
class Project;
class Renderer;
class AnimationSystem;
class AnimationGraphPanel {
public:
    AnimationGraphPanel(Project& project, Renderer& renderer);
    ~AnimationGraphPanel();
    AnimationGraphPanel(const AnimationGraphPanel&) = delete;
    AnimationGraphPanel& operator=(const AnimationGraphPanel&) = delete;
    void relocate(std::string_view oldPath, std::string_view newPath, bool directory);
    void open(std::string path);
    void draw(AnimationSystem* animation, EntityId entity);
private:
    void load();
    Project& m_project;
    Renderer& m_renderer;
    ImNodesContext* m_context = nullptr;
    AnimationGraph m_graph;
    std::string m_path, m_error, m_pendingPath, m_mesh;
    int m_state = -1, m_transition = -1;
    bool m_open = false, m_dirty = false, m_positions = false;
};
}
