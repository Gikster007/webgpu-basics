#pragma once

#include <webgpu/webgpu.hpp>

#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif // __EMSCRIPTEN__

using namespace wgpu;

class Application
{
  public:
    // Initialize everything and return true if it went all right
    bool initialize();

    // Uninitialize everything that was initialized
    void terminate();

    // Draw a frame and handle events
    void tick();

    // Return true as long as the main loop should keep on running
    bool is_running();

  private:
    TextureView get_next_surface_texture_view();

    // Substep of Initialize() that creates the render pipeline
    void initialize_pipeline();

  private:
    // We put here all the variables that are shared between init and main loop
    GLFWwindow* window;
    Device device;
    Queue queue;
    Surface surface;
    std::unique_ptr<ErrorCallback> uncaptured_error_callback_handle;
    TextureFormat surface_format = TextureFormat::Undefined;
    RenderPipeline pipeline;
};