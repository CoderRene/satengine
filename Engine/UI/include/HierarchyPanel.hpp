#pragma once

#include <memory>

namespace sat {

class Scene;

class HierarchyPanel {
public:
    explicit HierarchyPanel(Scene& scene);
    ~HierarchyPanel();

    HierarchyPanel(const HierarchyPanel&) = delete;
    HierarchyPanel& operator=(const HierarchyPanel&) = delete;

    void onImGuiRender(bool readOnly = false);
    [[nodiscard]] int selectedIndex() const;
    void setSelectedIndex(int index);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sat
