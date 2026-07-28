#include <DustEngine.hpp>

#include <chrono>
#include <cstdio>
#include "DustEngine.hpp"
int main() {
    Dust::DustEngine e;
    e.windows.create({ .name="main", .title="DustEngine", .width=1280, .height=720 });
    e.run([&](float dt) {
            // your game logic here
        });
    return 0;
}
