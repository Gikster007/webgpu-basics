#pragma once

#include <webgpu/webgpu.hpp>

#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif // __EMSCRIPTEN__

#include <filesystem>
#include <array>

using namespace wgpu;
namespace fs = std::filesystem;

/**
 * The same structure as in the shader, replicated in C++
 */
struct MyUniforms
{
    std::array<float, 4> color;
    float time;
    float _pad[3];
};
// Have the compiler check byte alignment
static_assert(sizeof(MyUniforms) % 16 == 0);

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

    RequiredLimits get_required_limits(Adapter adapter) const;

    void initialize_buffers();

    void initialize_pipeline(BindGroupLayoutDescriptor& bind_group_layout_desc,
                             BindGroupLayout& bind_group_layout);
   
    bool load_geometry(const fs::path& path, std::vector<float>& point_data,
                       std::vector<uint16_t>& index_data, int dimensions);

    ShaderModule load_shader_module(const fs::path& path, Device device);

  private:
    // We put here all the variables that are shared between init and main loop
    GLFWwindow* window;
    Device device;
    Queue queue;
    Surface surface;
    std::unique_ptr<ErrorCallback> uncaptured_error_callback_handle;
    TextureFormat surface_format = TextureFormat::Undefined;
    RenderPipeline pipeline;
    BindGroup bind_group;

    // Application attributes
    Buffer point_buffer;
    Buffer index_buffer;
    Buffer uniform_buffer;
    uint32_t index_count;
    MyUniforms uniforms;
    uint32_t uniform_stride;
};