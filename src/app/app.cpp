#include "app.h"
#include "../util/resource-manager.h"

#include <glfw3webgpu.h>
#include <GLFW/glfw3.h>

using VertexAttributes = ResourceManager::VertexAttributes;

bool Application::initialize()
{
    if (!init_window_and_device())
        return false;
    if (!init_swap_chain())
        return false;
    if (!init_depth_buffer())
        return false;
    if (!init_render_pipeline())
        return false;
    if (!init_texture())
        return false;
    if (!init_geometry())
        return false;
    if (!init_uniforms())
        return false;
    if (!init_bind_group())
        return false;
    return true;
}

void Application::tick()
{
    glfwPollEvents();

    // Update uniform buffer
    uniforms.time = static_cast<float>(glfwGetTime());
    queue.writeBuffer(uniform_buffer, offsetof(MyUniforms, time), &uniforms.time, sizeof(MyUniforms::time));

    TextureView next_texture = swap_chain.getCurrentTextureView();
    if (!next_texture)
    {
        std::cerr << "Cannot acquire next swap chain texture" << std::endl;
        return;
    }

    CommandEncoderDescriptor command_encoder_desc;
    command_encoder_desc.label = "Command Encoder";
    CommandEncoder encoder = device.createCommandEncoder(command_encoder_desc);

    RenderPassDescriptor render_pass_desc = {};

    RenderPassColorAttachment render_pass_color_attachment = {};
    render_pass_color_attachment.view = next_texture;
    render_pass_color_attachment.resolveTarget = nullptr;
    render_pass_color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED; // depthSlice must not be 0, unless the color attachment is a 3D texture
    render_pass_color_attachment.loadOp = LoadOp::Clear;
    render_pass_color_attachment.storeOp = StoreOp::Store;
    render_pass_color_attachment.clearValue = Color{0.1, 0.1, 0.1, 1.0};
    render_pass_desc.colorAttachmentCount = 1;
    render_pass_desc.colorAttachments = &render_pass_color_attachment;

    RenderPassDepthStencilAttachment depth_stencil_attachment;
    depth_stencil_attachment.view = depth_texture_view;
    depth_stencil_attachment.depthClearValue = 1.0f;
    depth_stencil_attachment.depthLoadOp = LoadOp::Clear;
    depth_stencil_attachment.depthStoreOp = StoreOp::Store;
    depth_stencil_attachment.depthReadOnly = false;
    depth_stencil_attachment.stencilClearValue = 0;
#ifdef WEBGPU_BACKEND_WGPU
    depth_stencil_attachment.stencilLoadOp = LoadOp::Clear;
    depth_stencil_attachment.stencilStoreOp = StoreOp::Store;
#else
    depth_stencil_attachment.stencilLoadOp = LoadOp::Undefined;
    depth_stencil_attachment.stencilStoreOp = StoreOp::Undefined;
#endif
    depth_stencil_attachment.stencilReadOnly = true;

    render_pass_desc.depthStencilAttachment = &depth_stencil_attachment;
    render_pass_desc.timestampWrites = nullptr;
    RenderPassEncoder render_pass = encoder.beginRenderPass(render_pass_desc);

    render_pass.setPipeline(pipeline);

    render_pass.setVertexBuffer(0, vertex_buffer, 0, vertex_count * sizeof(VertexAttributes));

    // Set binding group
    render_pass.setBindGroup(0, bind_group, 0, nullptr);

    render_pass.draw(vertex_count, 1, 0, 0);

    render_pass.end();
    render_pass.release();

    next_texture.release();

    CommandBufferDescriptor cmd_buffer_descriptor{};
    cmd_buffer_descriptor.label = "Command buffer";
    CommandBuffer command = encoder.finish(cmd_buffer_descriptor);
    encoder.release();
    queue.submit(command);
    command.release();

    swap_chain.present();

#ifdef WEBGPU_BACKEND_DAWN
    // Check for pending error callbacks
    device.tick();
#endif
}

void Application::terminate()
{
    terminate_bind_group();
    terminate_uniforms();
    terminate_geometry();
    terminate_texture();
    terminate_render_pipeline();
    terminate_depth_buffer();
    terminate_swap_chain();
    terminate_window_and_device();
}

bool Application::is_running()
{
    return !glfwWindowShouldClose(window);
}

void Application::on_resize()
{
    // Terminate in reverse order
    terminate_depth_buffer();
    terminate_swap_chain();

    // Re-initialise depth buffer and swap chain with correct win res
    init_swap_chain();
    init_depth_buffer();

    update_projection_matrix();
}

void Application::on_mouse_move(double xpos, double ypos)
{
    if (drag.active)
    {
        glm::vec2 current_mouse = glm::vec2(-(float)xpos, (float)ypos);
        glm::vec2 delta = (current_mouse - drag.start_mouse) * drag.sensitivity;
        camera_state.angles = drag.start_camera_state.angles + delta;
        // Clamp to avoid going too far when orbitting up/down
        camera_state.angles.y = glm::clamp(camera_state.angles.y, -PI / 2 + 1e-5f, PI / 2 - 1e-5f);
        update_view_matrix();

        // Inertia
        drag.velocity = delta - drag.previous_delta;
        drag.previous_delta = delta;
    }
}

void Application::on_mouse_button(int button, int action, int /*mods*/)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        switch (action)
        {
        case GLFW_PRESS:
            drag.active = true;
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            drag.start_mouse = glm::vec2(-(float)xpos, (float)ypos);
            drag.start_camera_state = camera_state;
            break;
        case GLFW_RELEASE:
            drag.active = false;
            break;
        }
    }
}

void Application::on_scroll(double /*xoffset*/, double yoffset)
{
    camera_state.zoom += drag.scroll_sensitivity * static_cast<float>(yoffset);
    camera_state.zoom = glm::clamp(camera_state.zoom, -2.0f, 2.0f);
    update_view_matrix();
}

bool Application::init_window_and_device()
{
    instance = createInstance(InstanceDescriptor{});
    if (!instance)
    {
        std::cerr << "Could not initialize WebGPU!" << std::endl;
        return false;
    }

    if (!glfwInit())
    {
        std::cerr << "Could not initialize GLFW!" << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window = glfwCreateWindow(1280, 720, "WebGPU Basics", NULL, NULL);
    if (!window)
    {
        std::cerr << "Could not open window!" << std::endl;
        return false;
    }

    // Add window callbacks
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int, int) {
        auto that = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        if (that != nullptr)
            that->on_resize();
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow* window, double xpos, double ypos) {
        auto that = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        if (that != nullptr)
            that->on_mouse_move(xpos, ypos);
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* window, int button, int action, int mods) {
        auto that = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        if (that != nullptr)
            that->on_mouse_button(button, action, mods);
    });
    glfwSetScrollCallback(window, [](GLFWwindow* window, double xoffset, double yoffset) {
        auto that = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        if (that != nullptr)
            that->on_scroll(xoffset, yoffset);
    });

    std::cout << "Requesting adapter..." << std::endl;
    surface = glfwGetWGPUSurface(instance, window);
    RequestAdapterOptions adapter_opts{};
    adapter_opts.compatibleSurface = surface;
    Adapter adapter = instance.requestAdapter(adapter_opts);
    std::cout << "Got adapter: " << adapter << std::endl;

    SupportedLimits supported_limits;
    adapter.getLimits(&supported_limits);

    std::cout << "Requesting device..." << std::endl;
    RequiredLimits required_limits = Default;
    required_limits.limits.maxVertexAttributes = 4;
    required_limits.limits.maxVertexBuffers = 1;
    required_limits.limits.maxBufferSize = 150000 * sizeof(VertexAttributes);
    required_limits.limits.maxVertexBufferArrayStride = sizeof(VertexAttributes);
    required_limits.limits.minStorageBufferOffsetAlignment = supported_limits.limits.minStorageBufferOffsetAlignment;
    required_limits.limits.minUniformBufferOffsetAlignment = supported_limits.limits.minUniformBufferOffsetAlignment;
    required_limits.limits.maxInterStageShaderComponents = 8;
    required_limits.limits.maxBindGroups = 1;
    required_limits.limits.maxUniformBuffersPerShaderStage = 1;
    required_limits.limits.maxUniformBufferBindingSize = 16 * 4 * sizeof(float);
    // Allow textures up to 2K
    required_limits.limits.maxTextureDimension1D = 2048;
    required_limits.limits.maxTextureDimension2D = 2048;
    required_limits.limits.maxTextureArrayLayers = 1;
    required_limits.limits.maxSampledTexturesPerShaderStage = 1;
    required_limits.limits.maxSamplersPerShaderStage = 1;

    DeviceDescriptor device_desc;
    device_desc.label = "My Device";
    device_desc.requiredFeatureCount = 0;
    device_desc.requiredLimits = &required_limits;
    device_desc.defaultQueue.label = "The default queue";
    device = adapter.requestDevice(device_desc);
    std::cout << "Got device: " << device << std::endl;

    // Add an error callback for more debug info
    error_callback_handle = device.setUncapturedErrorCallback([](ErrorType type, char const* message) {
        std::cout << "Device error: type " << type;
        if (message)
            std::cout << " (message: " << message << ")";
        std::cout << std::endl;
    });

    queue = device.getQueue();

#ifdef WEBGPU_BACKEND_WGPU
    swap_chain_format = surface.getPreferredFormat(adapter);
#else
    swap_chain_format = TextureFormat::BGRA8Unorm;
#endif

    adapter.release();
    return device != nullptr;
}

void Application::terminate_window_and_device()
{
    queue.release();
    device.release();
    surface.release();
    instance.release();

    glfwDestroyWindow(window);
    glfwTerminate();
}

bool Application::init_swap_chain()
{
    std::cout << "Creating swapchain..." << std::endl;

    // Get the current size of the window's framebuffer:
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    SwapChainDescriptor swap_chain_desc;
    swap_chain_desc.width = static_cast<uint32_t>(width);
    swap_chain_desc.height = static_cast<uint32_t>(height);
    swap_chain_desc.usage = TextureUsage::RenderAttachment;
    swap_chain_desc.format = swap_chain_format;
    swap_chain_desc.presentMode = PresentMode::Fifo;
    swap_chain = device.createSwapChain(surface, swap_chain_desc);
    std::cout << "Swapchain: " << swap_chain << std::endl;
    return swap_chain != nullptr;
}

void Application::terminate_swap_chain()
{
    swap_chain.release();
}

bool Application::init_depth_buffer()
{
    // Get the current size of the window's framebuffer:
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    // Create the depth texture
    TextureDescriptor depth_texture_desc;
    depth_texture_desc.dimension = TextureDimension::_2D;
    depth_texture_desc.format = depth_texture_format;
    depth_texture_desc.mipLevelCount = 1;
    depth_texture_desc.sampleCount = 1;
    depth_texture_desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    depth_texture_desc.usage = TextureUsage::RenderAttachment;
    depth_texture_desc.viewFormatCount = 1;
    depth_texture_desc.viewFormats = (WGPUTextureFormat*)&depth_texture_format;
    depth_texture = device.createTexture(depth_texture_desc);
    std::cout << "Depth texture: " << depth_texture << std::endl;

    // Create the view of the depth texture manipulated by the rasterizer
    TextureViewDescriptor depth_texture_view_desc;
    depth_texture_view_desc.aspect = TextureAspect::DepthOnly;
    depth_texture_view_desc.baseArrayLayer = 0;
    depth_texture_view_desc.arrayLayerCount = 1;
    depth_texture_view_desc.baseMipLevel = 0;
    depth_texture_view_desc.mipLevelCount = 1;
    depth_texture_view_desc.dimension = TextureViewDimension::_2D;
    depth_texture_view_desc.format = depth_texture_format;
    depth_texture_view = depth_texture.createView(depth_texture_view_desc);
    std::cout << "Depth texture view: " << depth_texture_view << std::endl;

    return depth_texture_view != nullptr;
}

void Application::terminate_depth_buffer()
{
    depth_texture_view.release();
    depth_texture.destroy();
    depth_texture.release();
}

bool Application::init_render_pipeline()
{
    std::cout << "Creating shader module..." << std::endl;
    shader_module = ResourceManager::load_shader_module(RESOURCE_DIR "/shader.wgsl", device);
    std::cout << "Shader module: " << shader_module << std::endl;

    std::cout << "Creating render pipeline..." << std::endl;
    RenderPipelineDescriptor pipeline_desc;

    // Vertex fetch
    std::vector<VertexAttribute> vertex_attribs(4);

    // Position attribute
    vertex_attribs[0].shaderLocation = 0;
    vertex_attribs[0].format = VertexFormat::Float32x3;
    vertex_attribs[0].offset = 0;

    // Normal attribute
    vertex_attribs[1].shaderLocation = 1;
    vertex_attribs[1].format = VertexFormat::Float32x3;
    vertex_attribs[1].offset = offsetof(VertexAttributes, normal);

    // Color attribute
    vertex_attribs[2].shaderLocation = 2;
    vertex_attribs[2].format = VertexFormat::Float32x3;
    vertex_attribs[2].offset = offsetof(VertexAttributes, color);

    // UV attribute
    vertex_attribs[3].shaderLocation = 3;
    vertex_attribs[3].format = VertexFormat::Float32x2;
    vertex_attribs[3].offset = offsetof(VertexAttributes, uv);

    VertexBufferLayout vertex_buffer_layout;
    vertex_buffer_layout.attributeCount = (uint32_t)vertex_attribs.size();
    vertex_buffer_layout.attributes = vertex_attribs.data();
    vertex_buffer_layout.arrayStride = sizeof(VertexAttributes);
    vertex_buffer_layout.stepMode = VertexStepMode::Vertex;

    pipeline_desc.vertex.bufferCount = 1;
    pipeline_desc.vertex.buffers = &vertex_buffer_layout;

    pipeline_desc.vertex.module = shader_module;
    pipeline_desc.vertex.entryPoint = "vs_main";
    pipeline_desc.vertex.constantCount = 0;
    pipeline_desc.vertex.constants = nullptr;

    pipeline_desc.primitive.topology = PrimitiveTopology::TriangleList;
    pipeline_desc.primitive.stripIndexFormat = IndexFormat::Undefined;
    pipeline_desc.primitive.frontFace = FrontFace::CCW;
    pipeline_desc.primitive.cullMode = CullMode::None;

    FragmentState fragment_state;
    pipeline_desc.fragment = &fragment_state;
    fragment_state.module = shader_module;
    fragment_state.entryPoint = "fs_main";
    fragment_state.constantCount = 0;
    fragment_state.constants = nullptr;

    BlendState blend_state;
    blend_state.color.srcFactor = BlendFactor::SrcAlpha;
    blend_state.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
    blend_state.color.operation = BlendOperation::Add;
    blend_state.alpha.srcFactor = BlendFactor::Zero;
    blend_state.alpha.dstFactor = BlendFactor::One;
    blend_state.alpha.operation = BlendOperation::Add;

    ColorTargetState color_target;
    color_target.format = swap_chain_format;
    color_target.blend = &blend_state;
    color_target.writeMask = ColorWriteMask::All;

    fragment_state.targetCount = 1;
    fragment_state.targets = &color_target;

    DepthStencilState depth_stencil_state = Default;
    depth_stencil_state.depthCompare = CompareFunction::Less;
    depth_stencil_state.depthWriteEnabled = true;
    depth_stencil_state.format = depth_texture_format;
    depth_stencil_state.stencilReadMask = 0;
    depth_stencil_state.stencilWriteMask = 0;

    pipeline_desc.depthStencil = &depth_stencil_state;

    pipeline_desc.multisample.count = 1;
    pipeline_desc.multisample.mask = ~0u;
    pipeline_desc.multisample.alphaToCoverageEnabled = false;

    // Create binding layouts
    std::vector<BindGroupLayoutEntry> binding_layout_entries(3, Default);

    // The uniform buffer binding that we already had
    BindGroupLayoutEntry& binding_layout = binding_layout_entries[0];
    binding_layout.binding = 0;
    binding_layout.visibility = ShaderStage::Vertex | ShaderStage::Fragment;
    binding_layout.buffer.type = BufferBindingType::Uniform;
    binding_layout.buffer.minBindingSize = sizeof(MyUniforms);

    // The texture binding
    BindGroupLayoutEntry& texture_binding_layout = binding_layout_entries[1];
    texture_binding_layout.binding = 1;
    texture_binding_layout.visibility = ShaderStage::Fragment;
    texture_binding_layout.texture.sampleType = TextureSampleType::Float;
    texture_binding_layout.texture.viewDimension = TextureViewDimension::_2D;

    // The texture sampler binding
    BindGroupLayoutEntry& sampler_binding_layout = binding_layout_entries[2];
    sampler_binding_layout.binding = 2;
    sampler_binding_layout.visibility = ShaderStage::Fragment;
    sampler_binding_layout.sampler.type = SamplerBindingType::Filtering;

    // Create a bind group layout
    BindGroupLayoutDescriptor bind_group_layout_desc{};
    bind_group_layout_desc.entryCount = (uint32_t)binding_layout_entries.size();
    bind_group_layout_desc.entries = binding_layout_entries.data();
    bind_group_layout = device.createBindGroupLayout(bind_group_layout_desc);

    // Create the pipeline layout
    PipelineLayoutDescriptor layout_desc{};
    layout_desc.bindGroupLayoutCount = 1;
    layout_desc.bindGroupLayouts = (WGPUBindGroupLayout*)&bind_group_layout;
    PipelineLayout layout = device.createPipelineLayout(layout_desc);
    pipeline_desc.layout = layout;

    pipeline = device.createRenderPipeline(pipeline_desc);
    std::cout << "Render pipeline: " << pipeline << std::endl;

    return pipeline != nullptr;
}

void Application::terminate_render_pipeline()
{
    pipeline.release();
    shader_module.release();
    bind_group_layout.release();
}

bool Application::init_texture()
{
    // Create a sampler
    SamplerDescriptor sampler_desc;
    sampler_desc.addressModeU = AddressMode::Repeat;
    sampler_desc.addressModeV = AddressMode::Repeat;
    sampler_desc.addressModeW = AddressMode::Repeat;
    sampler_desc.magFilter = FilterMode::Linear;
    sampler_desc.minFilter = FilterMode::Linear;
    sampler_desc.mipmapFilter = MipmapFilterMode::Linear;
    sampler_desc.lodMinClamp = 0.0f;
    sampler_desc.lodMaxClamp = 8.0f;
    sampler_desc.compare = CompareFunction::Undefined;
    sampler_desc.maxAnisotropy = 1;
    sampler = device.createSampler(sampler_desc);

    // Create a texture
    texture = ResourceManager::load_texture(RESOURCE_DIR "/fourareen2K_albedo.jpg", device, &texture_view);
    if (!texture)
    {
        std::cerr << "Could not load texture!" << std::endl;
        return false;
    }
    std::cout << "Texture: " << texture << std::endl;
    std::cout << "Texture view: " << texture_view << std::endl;

    return texture_view != nullptr;
}

void Application::terminate_texture()
{
    texture_view.release();
    texture.destroy();
    texture.release();
    sampler.release();
}

bool Application::init_geometry()
{
    // Load mesh data from OBJ file
    std::vector<VertexAttributes> vertex_data;
    bool success = ResourceManager::load_geometry_from_obj(RESOURCE_DIR "/fourareen.obj", vertex_data);
    if (!success)
    {
        std::cerr << "Could not load geometry!" << std::endl;
        return false;
    }

    // Create vertex buffer
    BufferDescriptor buffer_desc;
    buffer_desc.size = vertex_data.size() * sizeof(VertexAttributes);
    buffer_desc.usage = BufferUsage::CopyDst | BufferUsage::Vertex;
    buffer_desc.mappedAtCreation = false;
    vertex_buffer = device.createBuffer(buffer_desc);
    queue.writeBuffer(vertex_buffer, 0, vertex_data.data(), buffer_desc.size);

    vertex_count = static_cast<int>(vertex_data.size());

    return vertex_buffer != nullptr;
}

void Application::terminate_geometry()
{
    vertex_buffer.destroy();
    vertex_buffer.release();
    vertex_count = 0;
}

bool Application::init_uniforms()
{
    // Create uniform buffer
    BufferDescriptor buffer_desc;
    buffer_desc.size = sizeof(MyUniforms);
    buffer_desc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
    buffer_desc.mappedAtCreation = false;
    uniform_buffer = device.createBuffer(buffer_desc);

    // Upload the initial value of the uniforms
    uniforms.model = glm::mat4(1.0);
    uniforms.view = glm::lookAt(glm::vec3(-2.0f, -3.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0, 0, 1));
    uniforms.proj = glm::perspective(45 * PI / 180, 1280.0f / 720.0f, 0.01f, 100.0f);
    uniforms.time = 1.0f;
    uniforms.color = {0.0f, 1.0f, 0.4f, 1.0f};
    queue.writeBuffer(uniform_buffer, 0, &uniforms, sizeof(MyUniforms));

    update_view_matrix();

    return uniform_buffer != nullptr;
}

void Application::terminate_uniforms()
{
    uniform_buffer.destroy();
    uniform_buffer.release();
}

bool Application::init_bind_group()
{
    // Create a binding
    std::vector<BindGroupEntry> bindings(3);

    bindings[0].binding = 0;
    bindings[0].buffer = uniform_buffer;
    bindings[0].offset = 0;
    bindings[0].size = sizeof(MyUniforms);

    bindings[1].binding = 1;
    bindings[1].textureView = texture_view;

    bindings[2].binding = 2;
    bindings[2].sampler = sampler;

    BindGroupDescriptor bind_group_desc;
    bind_group_desc.layout = bind_group_layout;
    bind_group_desc.entryCount = (uint32_t)bindings.size();
    bind_group_desc.entries = bindings.data();
    bind_group = device.createBindGroup(bind_group_desc);

    return bind_group != nullptr;
}

void Application::terminate_bind_group()
{
    bind_group.release();
}

void Application::update_projection_matrix()
{
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    // In case window is minimised
    if (width == 0 || height == 0)
        return;
    float ratio = width / (float)height;
    uniforms.proj = glm::perspective(45 * PI / 180, ratio, 0.01f, 100.0f);
    queue.writeBuffer(uniform_buffer, offsetof(MyUniforms, proj), &uniforms.proj, sizeof(MyUniforms::proj));
}

void Application::update_view_matrix()
{
    float cx = cos(camera_state.angles.x);
    float sx = sin(camera_state.angles.x);
    float cy = cos(camera_state.angles.y);
    float sy = sin(camera_state.angles.y);
    glm::vec3 position = glm::vec3(cx * cy, sx * cy, sy) * std::exp(-camera_state.zoom);
    uniforms.view = glm::lookAt(position, glm::vec3(0.0f), glm::vec3(0, 0, 1));
    queue.writeBuffer(uniform_buffer, offsetof(MyUniforms, view), &uniforms.view, sizeof(MyUniforms::view));
}

void Application::update_drag_inertia()
{
    constexpr float eps = 1e-4f;
    // Apply inertia only when the user released the click.
    if (!drag.active)
    {
        // Avoid updating the matrix when the velocity is no longer noticeable
        if (std::abs(drag.velocity.x) < eps && std::abs(drag.velocity.y) < eps)
        {
            return;
        }
        camera_state.angles += drag.velocity;
        camera_state.angles.y = glm::clamp(camera_state.angles.y, -PI / 2 + 1e-5f, PI / 2 - 1e-5f);
        // Dampen the velocity so that it decreases exponentially and stops after a few frames.
        drag.velocity *= drag.intertia;
        update_view_matrix();
    }
}