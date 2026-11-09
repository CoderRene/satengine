#pragma once

#include <memory>

namespace sat {

class Scene;

class InspectorPanel {
public:
    explicit InspectorPanel(Scene& scene);
    ~InspectorPanel();

    InspectorPanel(const InspectorPanel&) = delete;
    InspectorPanel& operator=(const InspectorPanel&) = delete;

    void onImGuiRender(int selectedIndex);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sat
