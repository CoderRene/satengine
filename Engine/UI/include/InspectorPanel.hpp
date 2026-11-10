#pragma once

#include <memory>

namespace sat {

class Scene;
class Project;

class InspectorPanel {
public:
    InspectorPanel(Scene& scene, Project& project);
    ~InspectorPanel();

    InspectorPanel(const InspectorPanel&) = delete;
    InspectorPanel& operator=(const InspectorPanel&) = delete;

    void onImGuiRender(int selectedIndex);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sat
