#pragma once

#include <memory>

namespace sat {

class ContentPanel;
class Scene;
class Project;
struct Entity;

class InspectorPanel {
public:
    InspectorPanel(Scene& scene, Project& project);
    ~InspectorPanel();

    InspectorPanel(const InspectorPanel&) = delete;
    InspectorPanel& operator=(const InspectorPanel&) = delete;

    void onImGuiRender(int selectedIndex, Entity* liveEntity, ContentPanel* content);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sat
