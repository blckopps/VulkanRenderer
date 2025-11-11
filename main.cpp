#include "App.h"

int main() {
    vkapp::App app("My Vulkan App", 1280, 720);
    if (!app.Init()) return -1;
    app.Run();
    return 0;
}
