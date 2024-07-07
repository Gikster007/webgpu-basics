#include "app.h"

#include <fstream>
#include <sstream>

// We embbed the source of the shader module here
const char* shader_source = R"(
struct VertexInput {
    @location(0) position: vec2f,
    @location(1) color: vec3f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    // The location here does not refer to a vertex attribute, it just means
    // that this field must be handled by the rasterizer.
    // (It can also refer to another field of another struct that would be used
    // as input to the fragment shader.)
    @location(0) color: vec3f,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    let ratio = 1920.0 / 1080.0; // The width and height of the target surface
    var out: VertexOutput; // Create the output struct
    out.position = vec4f(in.position.x, in.position.y * ratio, 0.0, 1.0);
    out.color = in.color; // Send input color over to frag shader
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
	return vec4f(in.color, 1.0);
}
)";

// We define a function that hides implementation-specific variants of device polling:
void wgpu_poll_events([[maybe_unused]] Device device, [[maybe_unused]] bool yieldToWebBrowser)
{
#if defined(WEBGPU_BACKEND_DAWN)
    device.tick();
#elif defined(WEBGPU_BACKEND_WGPU)
    device.poll(false);
#elif defined(WEBGPU_BACKEND_EMSCRIPTEN)
    if (yieldToWebBrowser)
    {
        emscripten_sleep(100);
    }
#endif
}

bool Application::load_geometry(const fs::path& path, std::vector<float>& point_data,
                  std::vector<uint16_t>& index_data)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return false;
    }

    point_data.clear();
    index_data.clear();

    enum class Section
    {
        None,
        Points,
        Indices,
    };
    Section current_section = Section::None;

    float value;
    uint16_t index;
    std::string line;
    while (!file.eof())
    {
        getline(file, line);

        // overcome the `CRLF` problem
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (line == "[points]")
        {
            current_section = Section::Points;
        }
        else if (line == "[indices]")
        {
            current_section = Section::Indices;
        }
        else if (line[0] == '#' || line.empty())
        {
            // Do nothing, this is a comment
        }
        else if (current_section == Section::Points)
        {
            std::istringstream iss(line);
            // Get x, y, r, g, b
            for (int i = 0; i < 5; ++i)
            {
                iss >> value;
                point_data.push_back(value);
            }
        }
        else if (current_section == Section::Indices)
        {
            std::istringstream iss(line);
            // Get corners #0 #1 and #2
            for (int i = 0; i < 3; ++i)
            {
                iss >> index;
                index_data.push_back(index);
            }
        }
    }
    return true;
}

bool Application::initialize()
{
    // Open window
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(WIN_WIDTH, WIN_HEIGHT, "WebGPU basics", nullptr, nullptr);

    Instance instance = wgpuCreateInstance(nullptr);
    assert(instance && "CRITICAL: Instance failed to initialise");

    surface = glfwGetWGPUSurface(instance, window);
    assert(surface && "CRITICAL: Surface failed to initialise");

    std::cout << "Requesting adapter..." << std::endl;
    RequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = surface;
    Adapter adapter = instance.requestAdapter(adapterOpts);
    assert(adapter && "CRITICAL: Adapter failed to initialise");

    std::cout << "Got adapter: " << adapter << std::endl;

    instance.release();

    std::cout << "Requesting device..." << std::endl;
    DeviceDescriptor device_desc = {};
    device_desc.label = "My Device";
    device_desc.requiredFeatureCount = 0;
    device_desc.requiredLimits = nullptr;
    device_desc.defaultQueue.nextInChain = nullptr;
    device_desc.defaultQueue.label = "The default queue";
    device_desc.deviceLostCallback = [](WGPUDeviceLostReason reason, char const* message,
                                        void* /* pUserData */) {
        std::cout << "Device lost: reason " << reason;
        if (message)
            std::cout << " (" << message << ")";
        std::cout << std::endl;
    };

    RequiredLimits requiredLimits = get_required_limits(adapter);
    device_desc.requiredLimits = &requiredLimits;

    device = adapter.requestDevice(device_desc);
    assert(device && "CRITICAL: Device failed to initialise");
    std::cout << "Got device: " << device << std::endl;

    uncaptured_error_callback_handle =
        device.setUncapturedErrorCallback([](ErrorType type, char const* message) {
            std::cout << "Uncaptured device error: type " << type;
            if (message)
                std::cout << " (" << message << ")";
            std::cout << std::endl;
        });

    queue = device.getQueue();
    assert(queue && "CRITICAL: Queue failed to initialise");

    // Configure the surface
    SurfaceConfiguration config = {};

    // Configuration of the textures created for the underlying swap chain
    config.width = WIN_WIDTH;
    config.height = WIN_HEIGHT;
    config.usage = TextureUsage::RenderAttachment;
    surface_format = surface.getPreferredFormat(adapter);
    config.format = surface_format;

    // And we do not need any particular view format:
    config.viewFormatCount = 0;
    config.viewFormats = nullptr;
    config.device = device;
    config.presentMode = PresentMode::Fifo;
    config.alphaMode = CompositeAlphaMode::Auto;

    surface.configure(config);
    assert(surface && "CRITICAL: Surface failed to Configure");

    SupportedLimits supported_limits;

    adapter.getLimits(&supported_limits);
    std::cout << "adapter.maxVertexAttributes: " << supported_limits.limits.maxVertexAttributes
              << std::endl;

    device.getLimits(&supported_limits);
    std::cout << "device.maxVertexAttributes: " << supported_limits.limits.maxVertexAttributes
              << std::endl;

    initialize_pipeline();

    // Release the adapter only after it has been fully utilized
    adapter.release();

    BufferDescriptor buffer_desc;
    buffer_desc.label = "Some GPU-side data buffer";
    buffer_desc.usage = BufferUsage::CopyDst | BufferUsage::CopySrc;
    buffer_desc.size = 16;
    buffer_desc.mappedAtCreation = false;
    Buffer buffer1 = device.createBuffer(buffer_desc);

    buffer_desc.label = "Output buffer";
    buffer_desc.usage = BufferUsage::CopyDst | BufferUsage::MapRead;
    Buffer buffer2 = device.createBuffer(buffer_desc);

    // Create some CPU-side data buffer (of size 16 bytes)
    std::vector<uint8_t> numbers(16);
    for (uint8_t i = 0; i < 16; ++i)
        numbers[i] = i; // `numbers` now contains [ 0, 1, 2, ... ]

    // Copy this from `numbers` (RAM) to `buffer1` (VRAM)
    queue.writeBuffer(buffer1, 0, numbers.data(), numbers.size());

    CommandEncoder encoder = device.createCommandEncoder(Default);

    encoder.copyBufferToBuffer(buffer1, 0, buffer2, 0, 16);

    CommandBuffer command = encoder.finish(Default);
    encoder.release();
    queue.submit(1, &command);
    command.release();

    // The context shared between this main function and the callback.
    struct Context
    {
        bool ready;
        Buffer buffer;
    };

    auto on_buffer2_mapped = [](WGPUBufferMapAsyncStatus status, void* p_user_data) {
        Context* context = reinterpret_cast<Context*>(p_user_data);
        context->ready = true;
        std::cout << "Buffer 2 mapped with status " << status << std::endl;
        if (status != BufferMapAsyncStatus::Success)
            return;

        // Get a pointer to wherever the driver mapped the GPU memory to the RAM
        uint8_t* bufferData = (uint8_t*)context->buffer.getConstMappedRange(0, 16);

        std::cout << "bufferData = [";
        for (int i = 0; i < 16; ++i)
        {
            if (i > 0)
                std::cout << ", ";
            std::cout << (int)bufferData[i];
        }
        std::cout << "]" << std::endl;

        // Then do not forget to unmap the memory
        context->buffer.unmap();
    };

    // Create the Context instance
    Context context = {false, buffer2};

    wgpuBufferMapAsync(buffer2, MapMode::Read, 0, 16, on_buffer2_mapped, (void*)&context);

    while (!context.ready)
    {
        wgpu_poll_events(device, true /* yieldToBrowser */);
    }

    initialize_buffers();

    buffer1.release();
    buffer2.release();

    return true;
}

void Application::terminate()
{
    index_buffer.release();
    point_buffer.release();
    color_buffer.release();
    pipeline.release();
    surface.unconfigure();
    queue.release();
    surface.release();
    device.release();
    glfwDestroyWindow(window);
    glfwTerminate();
}

void Application::tick()
{
    glfwPollEvents();

    // Get the next target texture view
    TextureView target_view = get_next_surface_texture_view();
    if (!target_view)
        return;

    // Create a command encoder for the draw call
    CommandEncoderDescriptor encoder_desc = {};
    encoder_desc.label = "My command encoder";
    CommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoder_desc);

    // Create the render pass that clears the screen with our color
    RenderPassDescriptor render_pass_desc = {};

    // The attachment part of the render pass descriptor describes the target texture of the pass
    RenderPassColorAttachment render_pass_color_attachment = {};
    render_pass_color_attachment.view = target_view;
    render_pass_color_attachment.resolveTarget = nullptr;
    render_pass_color_attachment.loadOp = LoadOp::Clear;
    render_pass_color_attachment.storeOp = StoreOp::Store;
    render_pass_color_attachment.clearValue = WGPUColor{0.1f, 0.1f, 0.1f, 1.0f};
#ifndef WEBGPU_BACKEND_WGPU
    render_pass_color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif // NOT WEBGPU_BACKEND_WGPU

    render_pass_desc.colorAttachmentCount = 1;
    render_pass_desc.colorAttachments = &render_pass_color_attachment;
    render_pass_desc.depthStencilAttachment = nullptr;
    render_pass_desc.timestampWrites = nullptr;

    RenderPassEncoder render_pass = encoder.beginRenderPass(render_pass_desc);

    // Select which render pipeline to use
    render_pass.setPipeline(pipeline);

    // Set vertex buffer while encoding the render pass
    render_pass.setVertexBuffer(0, point_buffer, 0, point_buffer.getSize());
    render_pass.setVertexBuffer(1, color_buffer, 0, color_buffer.getSize());
    // The second argument must correspond to the choice of uint16_t or uint32_t
    // we've done when creating the index buffer.
    render_pass.setIndexBuffer(index_buffer, IndexFormat::Uint16, 0, index_buffer.getSize());

    // We use the `vertexCount` variable instead of hard-coding the vertex count
    render_pass.drawIndexed(index_count, 1, 0, 0, 0);

    render_pass.end();
    render_pass.release();

    // Finally encode and submit the render pass
    CommandBufferDescriptor cmd_buffer_descriptor = {};
    cmd_buffer_descriptor.label = "Command buffer";
    CommandBuffer command = encoder.finish(cmd_buffer_descriptor);
    encoder.release();

    std::cout << "Submitting command..." << std::endl;
    queue.submit(1, &command);
    command.release();
    std::cout << "Command submitted." << std::endl;

    // At the end of the frame
    target_view.release();
#ifndef __EMSCRIPTEN__
    surface.present();
#endif

#if defined(WEBGPU_BACKEND_DAWN)
    device.tick();
#elif defined(WEBGPU_BACKEND_WGPU)
    device.poll(false);
#endif
}

bool Application::is_running()
{
    return !glfwWindowShouldClose(window);
}

TextureView Application::get_next_surface_texture_view()
{
    // Get the surface texture
    SurfaceTexture surface_texture;
    surface.getCurrentTexture(&surface_texture);
    if (surface_texture.status != SurfaceGetCurrentTextureStatus::Success)
    {
        return nullptr;
    }
    Texture texture = surface_texture.texture;

    // Create a view for this surface texture
    TextureViewDescriptor view_descriptor;
    view_descriptor.label = "Surface texture view";
    view_descriptor.format = texture.getFormat();
    view_descriptor.dimension = TextureViewDimension::_2D;
    view_descriptor.baseMipLevel = 0;
    view_descriptor.mipLevelCount = 1;
    view_descriptor.baseArrayLayer = 0;
    view_descriptor.arrayLayerCount = 1;
    view_descriptor.aspect = TextureAspect::All;
    TextureView target_view = texture.createView(view_descriptor);

    return target_view;
}

RequiredLimits Application::get_required_limits(Adapter adapter) const
{
    // Get adapter supported limits, in case we need them
    SupportedLimits supported_limits;
    adapter.getLimits(&supported_limits);

    // Don't forget to = Default
    RequiredLimits required_limits = Default;

    // We use at most 1 vertex attribute for now
    required_limits.limits.maxVertexAttributes = 2;
    // We should also tell that we use 1 vertex buffers
    required_limits.limits.maxVertexBuffers = 2;
    // Maximum size of a buffer is 6 vertices of 2 float each
    required_limits.limits.maxBufferSize = 15 * 5 * sizeof(float);
    // Maximum stride between 2 consecutive vertices in the vertex buffer
    required_limits.limits.maxVertexBufferArrayStride = 3 * sizeof(float);
    // This must be set even if we do not use storage buffers for now
    required_limits.limits.minStorageBufferOffsetAlignment =
        supported_limits.limits.minStorageBufferOffsetAlignment;
    // There is a maximum of 3 float forwarded from vertex to fragment shader
    required_limits.limits.maxInterStageShaderComponents = 3;

    return required_limits;
}

void Application::initialize_buffers()
{
    // Vertex buffer data
    // There are 2 floats per vertex, one for x and one for y.
    // But in the end this is just a bunch of floats to the eyes of the GPU,
    // the *layout* will tell how to interpret this.
    // (0.0f, 0.0f) is the center of the screen
    std::vector<float> point_data = {
        -0.5f, -0.5f,
        +0.5f, -0.5f,
        +0.5f, +0.5f,
        -0.5f, +0.5f
    };

    // r0,  g0,  b0, r1,  g1,  b1, ...
    std::vector<float> color_data = {
        1.0, 0.0, 0.0, 
        0.0, 1.0, 0.0, 
        0.0, 0.0, 1.0,
        1.0, 1.0, 0.0,
    };

    // Define index data
    // This is a list of indices referencing positions in the point_data
    std::vector<uint16_t> index_data = {
        0, 1, 2, // Triangle #0 connects points #0, #1 and #2
        0, 2, 3  // Triangle #1 connects points #0, #2 and #3
    };

    // We will declare vertex_count as a member of the Application class
    index_count = static_cast<uint32_t>(index_data.size());
    assert(static_cast<uint32_t>(point_data.size() / 2) == static_cast<uint32_t>(color_data.size() / 3) &&
           "Amount of Positions doesn't match Amount of Colors");

    // Create buffer descriptor
    BufferDescriptor buffer_desc;
    buffer_desc.usage = BufferUsage::CopyDst | BufferUsage::Vertex;
    buffer_desc.mappedAtCreation = false;

    // Create vertex buffer
    buffer_desc.label = "Vertex Postition";
    buffer_desc.size = point_data.size() * sizeof(float);
    point_buffer = device.createBuffer(buffer_desc);
    queue.writeBuffer(point_buffer, 0, point_data.data(), buffer_desc.size);

    buffer_desc.label = "Vertex Color";
    buffer_desc.size = color_data.size() * sizeof(float);
    color_buffer = device.createBuffer(buffer_desc);
    queue.writeBuffer(color_buffer, 0, color_data.data(), buffer_desc.size);

    // Create index buffer
    buffer_desc.size = index_data.size() * sizeof(uint16_t);
    buffer_desc.size = (buffer_desc.size + 3) & ~3; // round up to the next multiple of 4
    buffer_desc.usage = BufferUsage::CopyDst | BufferUsage::Index;
    index_buffer = device.createBuffer(buffer_desc);
    queue.writeBuffer(index_buffer, 0, index_data.data(), buffer_desc.size);
}

void Application::initialize_pipeline()
{
    std::cout << "Initializing Pipeline" << std::endl;

    // Load the shader module
    ShaderModuleDescriptor shader_desc;
#ifdef WEBGPU_BACKEND_WGPU
    shader_desc.hintCount = 0;
    shader_desc.hints = nullptr;
#endif

    // We use the extension mechanism to specify the WGSL part of the shader module descriptor
    ShaderModuleWGSLDescriptor shader_code_desc;
    // Set the chained struct's header
    shader_code_desc.chain.next = nullptr;
    shader_code_desc.chain.sType = SType::ShaderModuleWGSLDescriptor;
    // Connect the chain
    shader_desc.nextInChain = &shader_code_desc.chain;
    shader_code_desc.code = shader_source;
    ShaderModule shader_module = device.createShaderModule(shader_desc);
    assert(shader_module && "CRITICAL: Shader Module failed to Create");

    // Create the render pipeline
    RenderPipelineDescriptor pipeline_desc;

    // We now have 2 attributes
    std::vector<VertexBufferLayout> vertex_buffer_layouts(2);

    // Position attribute
    VertexAttribute position_attrib;
    position_attrib.shaderLocation = 0;               // @location(0)
    position_attrib.format = VertexFormat::Float32x2; // size of position
    position_attrib.offset = 0;

    vertex_buffer_layouts[0].attributeCount = 1;
    vertex_buffer_layouts[0].attributes = &position_attrib;
    vertex_buffer_layouts[0].arrayStride = 2 * sizeof(float); // stride = size of position
    vertex_buffer_layouts[0].stepMode = VertexStepMode::Vertex;

    // Color attribute
    VertexAttribute color_attrib;
    color_attrib.shaderLocation = 1;               // @location(1)
    color_attrib.format = VertexFormat::Float32x3; // size of color
    color_attrib.offset = 0;

    vertex_buffer_layouts[1].attributeCount = 1;
    vertex_buffer_layouts[1].attributes = &color_attrib;
    vertex_buffer_layouts[1].arrayStride = 3 * sizeof(float); // stride = size of color
    vertex_buffer_layouts[1].stepMode = VertexStepMode::Vertex;


    pipeline_desc.vertex.bufferCount = static_cast<uint32_t>(vertex_buffer_layouts.size());
    pipeline_desc.vertex.buffers = vertex_buffer_layouts.data();

    // NB: We define the 'shader_module' in the second part of this chapter.
    // Here we tell that the programmable vertex shader stage is described
    // by the function called 'vs_main' in that module.
    pipeline_desc.vertex.module = shader_module;
    pipeline_desc.vertex.entryPoint = "vs_main";
    pipeline_desc.vertex.constantCount = 0;
    pipeline_desc.vertex.constants = nullptr;

    // Each sequence of 3 vertices is considered as a triangle
    pipeline_desc.primitive.topology = PrimitiveTopology::TriangleList;

    // We'll see later how to specify the order in which vertices should be
    // connected. When not specified, vertices are considered sequentially.
    pipeline_desc.primitive.stripIndexFormat = IndexFormat::Undefined;

    // The face orientation is defined by assuming that when looking
    // from the front of the face, its corner vertices are enumerated
    // in the counter-clockwise (CCW) order.
    pipeline_desc.primitive.frontFace = FrontFace::CCW;

    // But the face orientation does not matter much because we do not
    // cull (i.e. "hide") the faces pointing away from us (which is often
    // used for optimization).
    pipeline_desc.primitive.cullMode = CullMode::None;

    // We tell that the programmable fragment shader stage is described
    // by the function called 'fs_main' in the shader module.
    FragmentState fragment_state;
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
    color_target.format = surface_format;
    color_target.blend = &blend_state;
    color_target.writeMask =
        ColorWriteMask::All; // We could write to only some of the color channels.

    // We have only one target because our render pass has only one output color
    // attachment.
    fragment_state.targetCount = 1;
    fragment_state.targets = &color_target;
    pipeline_desc.fragment = &fragment_state;

    // We do not use stencil/depth testing for now
    pipeline_desc.depthStencil = nullptr;

    // Samples per pixel
    pipeline_desc.multisample.count = 1;

    // Default value for the mask, meaning "all bits on"
    pipeline_desc.multisample.mask = ~0u;

    // Default value as well (irrelevant for count = 1 anyways)
    pipeline_desc.multisample.alphaToCoverageEnabled = false;
    pipeline_desc.layout = nullptr;

    pipeline = device.createRenderPipeline(pipeline_desc);
    assert(pipeline && "CRITICAL: Pipeline failed to Create");

    // We no longer need to access the shader module
    shader_module.release();

    std::cout << "Pipeline Initialized!" << std::endl;
}