#pragma once

#include <webgpu/webgpu.hpp>

using namespace wgpu;

struct GLFWwindow;

// The same structure as in the shader, replicated in C++
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

struct CameraState
{
    // angles.x is the rotation of the camera around the global vertical axis, affected by mouse.x
    // angles.y is the rotation of the camera around its local horizontal axis, affected by mouse.y
    glm::vec2 angles = {0.8f, 0.5f};
    // zoom is the position of the camera along its local forward axis, affected by the scroll wheel
    float zoom = -1.2f;
};

struct DragState
{
    // Whether a drag action is ongoing (i.e., we are between mouse press and mouse release)
    bool active = false;
    // The position of the mouse at the beginning of the drag action
    glm::vec2 start_mouse;
    // The camera state at the beginning of the drag action
    CameraState start_camera_state;

    // Constant settings
    float sensitivity = 0.005f;
    float scroll_sensitivity = 0.1f;
    // Inertia
    glm::vec2 velocity = {0.0, 0.0};
    glm::vec2 previous_delta;
    float intertia = 0.9f;
};

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

    // A function called when the window is resized
    void on_resize();

    // Mouse events
    void on_mouse_move(double xpos, double ypos);
    void on_mouse_button(int button, int action, int mods);
    void on_scroll(double xoffset, double yoffset);

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

    // Camera Related
    void update_projection_matrix();
    void update_view_matrix();
    void update_drag_inertia();

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

    // Camera
    CameraState camera_state;
    DragState drag;

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