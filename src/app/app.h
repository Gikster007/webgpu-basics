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
    glm::mat4 proj;
    glm::mat4 view;
    glm::mat4 model;
    glm::vec4 color;
    float time;
    float _pad[3];
};
// Have the compiler check byte alignment
static_assert(sizeof(MyUniforms) % 16 == 0);

/**
 * A structure that describes the data layout in the vertex buffer
 * We do not instantiate it but use it in `sizeof` and `offsetof`
 */
struct VertexAttributes
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;
};

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

    bool load_geometry_from_obj(const fs::path& path, std::vector<VertexAttributes>& vertex_data);

    Texture load_texture(const fs::path& path, Device device, TextureView* texture_view = nullptr);

    ShaderModule load_shader_module(const fs::path& path, Device device);

    static void write_mip_maps(Device device, Texture texture, Extent3D texture_size,
                             [[maybe_unused]] uint32_t mip_level_count, // not used yet
                             const unsigned char* pixel_data);

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

    Texture depth_texture;
    TextureView depth_texture_view;
    Sampler sampler;

    // Application attributes
    Buffer vertex_buffer;
    Buffer uniform_buffer;
    uint32_t index_count;
    MyUniforms uniforms;
    uint32_t uniform_stride;
};