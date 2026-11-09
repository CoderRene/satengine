#pragma once

namespace sat {

class Application {
public:
    [[nodiscard]] int run();

private:
    void onStart();
    void onUpdate();
    void onStop();
};

} // namespace sat