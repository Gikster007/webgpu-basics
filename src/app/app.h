#pragma once

#include <webgpu/webgpu.hpp>

using namespace wgpu;

struct GLFWwindow;

/**
 * The same structure as in the shader, replicated in C++
 */
struct MyUniforms
{
    glm::mat4 proj;
    glm::mat4 view;
    glm::mat4 model;
    glm::vec4 color;
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

    // Draw a frame and handle events
    void tick();

    // Uninitialize everything that was initialized
    void terminate();

    // Return true as long as the main loop should keep on running
    bool is_running();

  private:
    bool init_window_and_device();
    void terminate_window_and_device();

    bool init_swap_chain();
    void terminate_swap_chain();

    bool init_depth_buffer();
    void terminate_depth_buffer();

    bool init_render_pipeline();
    void terminate_render_pipeline();

    bool init_texture();
    void terminate_texture();

    bool init_geometry();
    void terminate_geometry();

    bool init_uniforms();
    void terminate_uniforms();

    bool init_bind_group();
    void terminate_bind_group();

  private:
    // Window and Device
    GLFWwindow* window = nullptr;
    Instance instance = nullptr;
    Surface surface = nullptr;
    Device device = nullptr;
    Queue queue = nullptr;
    TextureFormat swap_chain_format = TextureFormat::Undefined;
    // Keep the error callback alive
    std::unique_ptr<ErrorCallback> error_callback_handle;

    // Swap Chain
    SwapChain swap_chain = nullptr;

    // Depth Buffer
    TextureFormat depth_texture_format = TextureFormat::Depth24Plus;
    Texture depth_texture = nullptr;
    TextureView depth_texture_view = nullptr;

    // Render Pipeline
    BindGroupLayout bind_group_layout = nullptr;
    ShaderModule shader_module = nullptr;
    RenderPipeline pipeline = nullptr;

    // Texture
    Sampler sampler = nullptr;
    Texture texture = nullptr;
    TextureView texture_view = nullptr;

    // Geometry
    Buffer vertex_buffer = nullptr;
    int vertex_count = 0;

    // Uniforms
    Buffer uniform_buffer = nullptr;
    MyUniforms uniforms;

    // Bind Group
    BindGroup bind_group = nullptr;
};