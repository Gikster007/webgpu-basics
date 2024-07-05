#define WEBGPU_CPP_IMPLEMENTATION
#include "app/app.h"

int main()
{
    Application app;

    if (!app.initialize())
    {
        return 1;
    }

#ifdef __EMSCRIPTEN__
    // Equivalent of the main loop when using Emscripten:
    auto callback = [](void* arg) {
        Application* pApp = reinterpret_cast<Application*>(arg);
        pApp->tick(); // 4. We can use the application object
    };
    emscripten_set_main_loop_arg(callback, &app, 0, true);
#else  // __EMSCRIPTEN__
    while (app.is_running())
    {
        app.tick();
    }
#endif // __EMSCRIPTEN__

    app.terminate();

    return 0;
}