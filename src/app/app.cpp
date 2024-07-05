#include "app.h"

// We embbed the source of the shader module here
const char* shader_source = R"(
@vertex
fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> @builtin(position) vec4f {
	var p = vec2f(0.0, 0.0);
	if (in_vertex_index == 0u) {
		p = vec2f(-0.5, -0.5);
	} else if (in_vertex_index == 1u) {
		p = vec2f(0.5, -0.5);
	} else {
		p = vec2f(0.0, 0.5);
	}
	return vec4f(p, 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4f {
	return vec4f(0.0, 0.4, 1.0, 1.0);
}
)";

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

    // Release the adapter only after it has been fully utilized
    adapter.release();

    initialize_pipeline();

    return true;
}

void Application::terminate()
{
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
    render_pass_color_attachment.clearValue = WGPUColor{0.6, 0.1, 0.4, 1.0};
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
    // Draw 1 instance of a 3-vertices shape
    render_pass.draw(3, 1, 0, 0);

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

    // We do not use any vertex buffer for this first simplistic example
    pipeline_desc.vertex.bufferCount = 0;
    pipeline_desc.vertex.buffers = nullptr;

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
}