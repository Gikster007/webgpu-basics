#include "app.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "../tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"

#include <fstream>
#include <sstream>

// Equivalent of std::bit_width that is available from C++20 onward
uint32_t bit_width(uint32_t m)
{
    if (m == 0)
        return 0;
    else
    {
        uint32_t w = 0;
        while (m >>= 1)
            ++w;
        return w;
    }
}

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

/**
 * Round 'value' up to the next multiplier of 'step'.
 */
uint32_t ceil_to_next_multiple(uint32_t value, uint32_t step)
{
    uint32_t divide_and_ceil = value / step + (value % step == 0 ? 0 : 1);
    return step * divide_and_ceil;
}

bool Application::load_geometry(const fs::path& path, std::vector<float>& point_data, std::vector<uint16_t>& index_data, int dimensions)
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
            // Get x, y, (z,) r, g, b
            for (int i = 0; i < dimensions + 3; ++i)
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

bool Application::load_geometry_from_obj(const fs::path& path, std::vector<VertexAttributes>& vertex_data)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.string().c_str());

    if (!warn.empty())
    {
        std::cout << warn << std::endl;
    }

    if (!err.empty())
    {
        std::cerr << err << std::endl;
    }

    if (!ret)
    {
        return false;
    }

    // Filling in vertexData:
    vertex_data.clear();
    for (const auto& shape : shapes)
    {
        size_t offset = vertex_data.size();
        vertex_data.resize(offset + shape.mesh.indices.size());

        for (size_t i = 0; i < shape.mesh.indices.size(); ++i)
        {
            const tinyobj::index_t& idx = shape.mesh.indices[i];

            vertex_data[offset + i].position = {attrib.vertices[3 * idx.vertex_index + 0],
                                                -attrib.vertices[3 * idx.vertex_index + 2], // Add a minus to avoid mirroring
                                                attrib.vertices[3 * idx.vertex_index + 1]};

            // Also apply the transform to normals!!
            vertex_data[offset + i].normal = {attrib.normals[3 * idx.normal_index + 0], -attrib.normals[3 * idx.normal_index + 2],
                                              attrib.normals[3 * idx.normal_index + 1]};

            vertex_data[offset + i].color = {attrib.colors[3 * idx.vertex_index + 0], attrib.colors[3 * idx.vertex_index + 1],
                                             attrib.colors[3 * idx.vertex_index + 2]};

            // UV conversion due to differences between OBJ files and Modern Graphics APIs
            vertex_data[offset + i].uv = {attrib.texcoords[2 * idx.texcoord_index + 0], 1 - attrib.texcoords[2 * idx.texcoord_index + 1]};
        }
    }

    return true;
}

Texture Application::load_texture(const fs::path& path, Device device, TextureView* texture_view)
{
    int width, height, channels;
    unsigned char* pixel_data = stbi_load(path.string().c_str(), &width, &height, &channels, 4 /* force 4 channels */);
    if (nullptr == pixel_data)
        return nullptr;

    TextureDescriptor texture_desc;
    texture_desc.dimension = TextureDimension::_2D;
    texture_desc.format = TextureFormat::RGBA8Unorm; // by convention for bmp, png and jpg file. Be careful with other formats.
    texture_desc.size = {(unsigned int)width, (unsigned int)height, 1};
    texture_desc.mipLevelCount = bit_width(std::max(texture_desc.size.width, texture_desc.size.height));
    texture_desc.sampleCount = 1;
    texture_desc.usage = TextureUsage::TextureBinding | TextureUsage::CopyDst;
    texture_desc.viewFormatCount = 0;
    texture_desc.viewFormats = nullptr;
    Texture texture = device.createTexture(texture_desc);

    if (texture_view)
    {
        TextureViewDescriptor texture_view_desc;
        texture_view_desc.aspect = TextureAspect::All;
        texture_view_desc.baseArrayLayer = 0;
        texture_view_desc.arrayLayerCount = 1;
        texture_view_desc.baseMipLevel = 0;
        texture_view_desc.mipLevelCount = texture_desc.mipLevelCount;
        texture_view_desc.dimension = TextureViewDimension::_2D;
        texture_view_desc.format = texture_desc.format;
        *texture_view = texture.createView(texture_view_desc);
    }

    // Upload data to the GPU texture (to be implemented!)
    write_mip_maps(device, texture, texture_desc.size, texture_desc.mipLevelCount, pixel_data);

    stbi_image_free(pixel_data);

    return texture;
}

ShaderModule Application::load_shader_module(const fs::path& path, Device device)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return nullptr;
    }
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    std::string shader_source(size, ' ');
    file.seekg(0);
    file.read(shader_source.data(), size);

    ShaderModuleWGSLDescriptor shader_code_desc{};
    shader_code_desc.chain.next = nullptr;
    shader_code_desc.chain.sType = SType::ShaderModuleWGSLDescriptor;
    shader_code_desc.code = shader_source.c_str();

    ShaderModuleDescriptor shader_desc{};
#ifdef WEBGPU_BACKEND_WGPU
    shader_desc.hintCount = 0;
    shader_desc.hints = nullptr;
#endif
    shader_desc.nextInChain = &shader_code_desc.chain;

    return device.createShaderModule(shader_desc);
}

void Application::write_mip_maps(Device device, Texture texture, Extent3D texture_size, uint32_t mip_level_count, const unsigned char* pixel_data)
{
    Queue queue = device.getQueue();

    // Arguments telling which part of the texture we upload to
    ImageCopyTexture destination;
    destination.texture = texture;
    destination.origin = {0, 0, 0};
    destination.aspect = TextureAspect::All;

    // Arguments telling how the C++ side pixel memory is laid out
    TextureDataLayout source;
    source.offset = 0;

    // Create image data
    Extent3D mip_level_size = texture_size;
    std::vector<unsigned char> previous_level_pixels;
    Extent3D previous_mip_level_size;
    for (uint32_t level = 0; level < mip_level_count; ++level)
    {
        // Pixel data for the current level
        std::vector<unsigned char> pixels(4 * mip_level_size.width * mip_level_size.height);
        if (level == 0)
        {
            // We cannot really avoid this copy since we need this
            // in previous_level_pixels at the next iteration
            memcpy(pixels.data(), pixel_data, pixels.size());
        }
        else
        {
            // Create mip level data
            for (uint32_t i = 0; i < mip_level_size.width; ++i)
            {
                for (uint32_t j = 0; j < mip_level_size.height; ++j)
                {
                    unsigned char* p = &pixels[4 * (j * mip_level_size.width + i)];
                    // Get the corresponding 4 pixels from the previous level
                    unsigned char* p00 = &previous_level_pixels[4 * ((2 * j + 0) * previous_mip_level_size.width + (2 * i + 0))];
                    unsigned char* p01 = &previous_level_pixels[4 * ((2 * j + 0) * previous_mip_level_size.width + (2 * i + 1))];
                    unsigned char* p10 = &previous_level_pixels[4 * ((2 * j + 1) * previous_mip_level_size.width + (2 * i + 0))];
                    unsigned char* p11 = &previous_level_pixels[4 * ((2 * j + 1) * previous_mip_level_size.width + (2 * i + 1))];
                    // Average
                    p[0] = (p00[0] + p01[0] + p10[0] + p11[0]) / 4;
                    p[1] = (p00[1] + p01[1] + p10[1] + p11[1]) / 4;
                    p[2] = (p00[2] + p01[2] + p10[2] + p11[2]) / 4;
                    p[3] = (p00[3] + p01[3] + p10[3] + p11[3]) / 4;
                }
            }
        }

        // Upload data to the GPU texture
        destination.mipLevel = level;
        source.bytesPerRow = 4 * mip_level_size.width;
        source.rowsPerImage = mip_level_size.height;
        queue.writeTexture(destination, pixels.data(), pixels.size(), source, mip_level_size);

        previous_level_pixels = std::move(pixels);
        previous_mip_level_size = mip_level_size;
        mip_level_size.width /= 2;
        mip_level_size.height /= 2;
    }

    queue.release();
}

bool Application::initialize()
{
    // Open window
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(WIN_WIDTH, WIN_HEIGHT, "WebGPU Basics", nullptr, nullptr);

    Instance instance = wgpuCreateInstance(nullptr);
    assert(instance && "CRITICAL: Instance failed to initialise");

    surface = glfwGetWGPUSurface(instance, window);
    assert(surface && "CRITICAL: Surface failed to initialise");

    std::cout << "Requesting adapter..." << std::endl;
    RequestAdapterOptions adapter_opts = {};
    adapter_opts.compatibleSurface = surface;
    Adapter adapter = instance.requestAdapter(adapter_opts);
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
    device_desc.deviceLostCallback = [](WGPUDeviceLostReason reason, char const* message, void* /* pUserData */) {
        std::cout << "Device lost: reason " << reason;
        if (message)
            std::cout << " (" << message << ")";
        std::cout << std::endl;
    };
    RequiredLimits required_limits = get_required_limits(adapter);
    device_desc.requiredLimits = &required_limits;
    device = adapter.requestDevice(device_desc);
    assert(device && "CRITICAL: Device failed to initialise");
    std::cout << "Got device: " << device << std::endl;
    uncaptured_error_callback_handle = device.setUncapturedErrorCallback([](ErrorType type, char const* message) {
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
    std::cout << "adapter.maxVertexAttributes: " << supported_limits.limits.maxVertexAttributes << std::endl;

    device.getLimits(&supported_limits);
    std::cout << "device.maxVertexAttributes: " << supported_limits.limits.maxVertexAttributes << std::endl;
    Limits device_limits = supported_limits.limits;

    // Subtlety
    uniform_stride = ceil_to_next_multiple((uint32_t)sizeof(MyUniforms), (uint32_t)device_limits.minUniformBufferOffsetAlignment);

    // Layout for Uniform Buffer binding
    BindGroupLayoutDescriptor bind_group_layout_desc;
    BindGroupLayout bind_group_layout;

    initialize_pipeline(bind_group_layout_desc, bind_group_layout);

    // Release the adapter only after it has been fully utilized
    adapter.release();

    // Create a texture
    TextureView texture_view = nullptr;
    Texture texture = load_texture(RESOURCE_DIR "/fourareen2K_albedo.jpg", device, &texture_view);
    if (!texture)
    {
        std::cerr << "Could not load texture!" << std::endl;
        return false;
    }

    // Camera Setup
    glm::vec3 focal_point(0.0, 0.0, -2.0);
    float angle1 = 1.0;
    float angle2 = 3.0 * PI / 4.0;

    glm::mat4 scale = glm::scale(glm::mat4(1.0), glm::vec3(0.3));
    glm::mat4 trans1 = glm::translate(glm::mat4(1.0), glm::vec3(1.0, 0.0, 0.0));
    glm::mat4 rot1 = glm::rotate(glm::mat4(1.0), angle1, glm::vec3(0.0, 0.0, 1.0));
    uniforms.model = rot1 * trans1 * scale;

    glm::mat4 rot2 = glm::rotate(glm::mat4(1.0), -angle2, glm::vec3(1.0, 0.0, 0.0));
    glm::mat4 trans2 = glm::translate(glm::mat4(1.0), -focal_point);
    uniforms.view = trans2 * rot2;

    float near = 0.001f;
    float far = 100.0f;
    float ratio = WIN_RATIO;
    float fov = 45 * PI / 180;
    uniforms.proj = glm::perspective(fov, ratio, near, far);

    // QUAD TEXTURE TESTING
    uniforms.model = glm::mat4(1.0);
    uniforms.view = glm::lookAt(glm::vec3(-2.0f, -3.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0, 0, 1));
    uniforms.proj = glm::perspective(fov, ratio, near, far);

    initialize_buffers();

    // Create a binding
    std::vector<BindGroupEntry> bindings(3);
    // Uniform Buffer Binding
    bindings[0].binding = 0;
    bindings[0].buffer = uniform_buffer;
    bindings[0].offset = 0;
    bindings[0].size = sizeof(MyUniforms);
    // Quad Texture Binding
    bindings[1].binding = 1;
    bindings[1].textureView = texture_view;
    // Sampler Binding
    bindings[2].binding = 2;
    bindings[2].sampler = sampler;

    // A bind group contains one or multiple bindings
    BindGroupDescriptor bind_group_desc;
    bind_group_desc.layout = bind_group_layout;
    // There must be as many bindings as declared in the layout!
    bind_group_desc.entryCount = (uint32_t)bindings.size();
    bind_group_desc.entries = bindings.data();
    bind_group = device.createBindGroup(bind_group_desc);

    return true;
}

void Application::terminate()
{
    vertex_buffer.release();
    depth_texture_view.release();
    depth_texture.destroy();
    depth_texture.release();
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
    // We now add a depth/stencil attachment:
    RenderPassDepthStencilAttachment depth_stencil_attachment;
    // The view of the depth texture
    depth_stencil_attachment.view = depth_texture_view;
    // The initial value of the depth buffer, meaning "far"
    depth_stencil_attachment.depthClearValue = 1.0f;
    // Operation settings comparable to the color attachment
    depth_stencil_attachment.depthLoadOp = LoadOp::Clear;
    depth_stencil_attachment.depthStoreOp = StoreOp::Store;
    // we could turn off writing to the depth buffer globally here
    depth_stencil_attachment.depthReadOnly = false;
    // Stencil setup, mandatory but unused
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

    // Select which render pipeline to use
    render_pass.setPipeline(pipeline);

    // Set vertex buffer while encoding the render pass
    render_pass.setVertexBuffer(0, vertex_buffer, 0, vertex_buffer.getSize());
    uint32_t dynamicOffset = 0;

    // Set binding group
    dynamicOffset = 0 * uniform_stride;
    render_pass.setBindGroup(0, bind_group, 1, &dynamicOffset);

    render_pass.draw(index_count, 1, 0, 0);

    //// Set binding group with a different uniform offset
    // dynamicOffset = 1 * uniform_stride;
    // render_pass.setBindGroup(0, bind_group, 1, &dynamicOffset);
    // render_pass.drawIndexed(index_count, 1, 0, 0, 0);

    // Upload first values
    uniforms.time = /*1.0f*/ static_cast<float>(glfwGetTime());
    uniforms.color = {0.0f, 1.0f, 0.4f, 1.0f};
    queue.writeBuffer(uniform_buffer, offsetof(MyUniforms, view), &uniforms.view, sizeof(MyUniforms::view));

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

    // We use at most 3 vertex attribute
    required_limits.limits.maxVertexAttributes = 4;
    // We should also tell that we use 2 vertex buffers
    required_limits.limits.maxVertexBuffers = 2;
    // Maximum size of a buffer
    required_limits.limits.maxBufferSize = 150000 * sizeof(VertexAttributes);
    // Maximum stride between 2 consecutive vertices in the vertex buffer
    required_limits.limits.maxVertexBufferArrayStride = sizeof(VertexAttributes);
    // This must be set even if we do not use storage buffers for now
    required_limits.limits.minStorageBufferOffsetAlignment = supported_limits.limits.minStorageBufferOffsetAlignment;
    // There is a maximum of 6 float forwarded from vertex to fragment shader
    required_limits.limits.maxInterStageShaderComponents = 8;
    // We use at most 1 bind group for now
    required_limits.limits.maxBindGroups = 1;
    // We use at most 1 uniform buffer per stage
    required_limits.limits.maxUniformBuffersPerShaderStage = 1;
    // Uniform structs size
    required_limits.limits.maxUniformBufferBindingSize = 16 * 4 * sizeof(float);
    // Extra limit requirement
    required_limits.limits.maxDynamicUniformBuffersPerPipelineLayout = 1;
    // Allow textures up to 2K
    required_limits.limits.maxTextureDimension1D = 2048;
    required_limits.limits.maxTextureDimension2D = 2048;
    required_limits.limits.maxTextureArrayLayers = 1;
    // Add the possibility to sample a texture in a shader
    required_limits.limits.maxSampledTexturesPerShaderStage = 1;
    // Set amount of samplers
    required_limits.limits.maxSamplersPerShaderStage = 1;

    return required_limits;
}

void Application::initialize_buffers()
{
    std::vector<VertexAttributes> vertex_data;
    bool success = load_geometry_from_obj(RESOURCE_DIR "/fourareen.obj", vertex_data);
    if (!success)
    {
        std::cerr << "Could not load geometry!" << std::endl;
        return;
    }
    assert(success && "Could not load geometry!");

    // Create buffer descriptor
    BufferDescriptor buffer_desc;
    buffer_desc.usage = BufferUsage::CopyDst | BufferUsage::Vertex;
    buffer_desc.mappedAtCreation = false;

    // Create vertex buffer
    buffer_desc.label = "Vertex Buffer";
    buffer_desc.size = vertex_data.size() * sizeof(VertexAttributes);
    vertex_buffer = device.createBuffer(buffer_desc);
    queue.writeBuffer(vertex_buffer, 0, vertex_data.data(), buffer_desc.size);

    index_count = static_cast<int>(vertex_data.size());

    // Create uniform buffer
    buffer_desc.label = "Uniform Buffer";
    buffer_desc.size = uniform_stride + sizeof(MyUniforms);
    buffer_desc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
    buffer_desc.mappedAtCreation = false;
    uniform_buffer = device.createBuffer(buffer_desc);
    // Upload the initial value of the uniforms
    uniforms.time = 1.0f;
    uniforms.color = {0.0f, 1.0f, 0.4f, 1.0f};
    queue.writeBuffer(uniform_buffer, 0, &uniforms, sizeof(MyUniforms));
}

void Application::initialize_pipeline(BindGroupLayoutDescriptor& bind_group_layout_desc, BindGroupLayout& bind_group_layout)
{
    std::cout << "Initializing Pipeline" << std::endl;

    std::cout << "Creating shader module..." << std::endl;
    ShaderModule shader_module = load_shader_module(RESOURCE_DIR "/shader.wgsl", device);
    std::cout << "Shader module: " << shader_module << std::endl;
    assert(shader_module && "CRITICAL: Shader Module failed to Create");

    // Create the render pipeline
    RenderPipelineDescriptor pipeline_desc;

    // We use one vertex buffer
    VertexBufferLayout vertex_buffer_layout;
    // We now have 2 attributes
    std::vector<VertexAttribute> vertex_attribs(4);

    // Describe the position attribute
    vertex_attribs[0].shaderLocation = 0; // @location(0)
    vertex_attribs[0].format = VertexFormat::Float32x3;
    vertex_attribs[0].offset = offsetof(VertexAttributes, position);

    // Describe the normal attribute
    vertex_attribs[1].shaderLocation = 1;
    vertex_attribs[1].format = VertexFormat::Float32x3;
    vertex_attribs[1].offset = offsetof(VertexAttributes, normal);

    // Describe the color attribute
    vertex_attribs[2].shaderLocation = 2;
    vertex_attribs[2].format = VertexFormat::Float32x3;
    vertex_attribs[2].offset = offsetof(VertexAttributes, color);

    // UV attribute
    vertex_attribs[3].shaderLocation = 3;
    vertex_attribs[3].format = VertexFormat::Float32x2;
    vertex_attribs[3].offset = offsetof(VertexAttributes, uv);

    vertex_buffer_layout.attributeCount = static_cast<uint32_t>(vertex_attribs.size());
    vertex_buffer_layout.attributes = vertex_attribs.data();

    vertex_buffer_layout.arrayStride = sizeof(VertexAttributes);
    vertex_buffer_layout.stepMode = VertexStepMode::Vertex;

    pipeline_desc.vertex.bufferCount = 1;
    pipeline_desc.vertex.buffers = &vertex_buffer_layout;

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
    color_target.writeMask = ColorWriteMask::All; // We could write to only some of the color channels.

    // We have only one target because our render pass has only one output color
    // attachment.
    fragment_state.targetCount = 1;
    fragment_state.targets = &color_target;
    pipeline_desc.fragment = &fragment_state;

    // We setup a depth buffer state for the render pipeline
    DepthStencilState depth_stencil_state = Default;
    // Keep a fragment only if its depth is lower than the previously blended one
    depth_stencil_state.depthCompare = CompareFunction::Less;
    // Each time a fragment is blended into the target, we update the value of the Z-buffer
    depth_stencil_state.depthWriteEnabled = true;
    // Store the format in a variable as later parts of the code depend on it
    TextureFormat depth_texture_format = TextureFormat::Depth24Plus;
    depth_stencil_state.format = depth_texture_format;
    // Deactivate the stencil alltogether
    depth_stencil_state.stencilReadMask = 0;
    depth_stencil_state.stencilWriteMask = 0;

    pipeline_desc.depthStencil = &depth_stencil_state;

    // Samples per pixel
    pipeline_desc.multisample.count = 1;

    // Default value for the mask, meaning "all bits on"
    pipeline_desc.multisample.mask = ~0u;

    // Default value as well (irrelevant for count = 1 anyways)
    pipeline_desc.multisample.alphaToCoverageEnabled = false;
    pipeline_desc.layout = nullptr;

    
    std::vector<BindGroupLayoutEntry> binding_layout_entries(3, Default);

    // The uniform buffer binding that we already had
    BindGroupLayoutEntry& binding_layout = binding_layout_entries[0];
    binding_layout.binding = 0;
    binding_layout.visibility = ShaderStage::Vertex | ShaderStage::Fragment;
    binding_layout.buffer.type = BufferBindingType::Uniform;
    binding_layout.buffer.minBindingSize = sizeof(MyUniforms);
    binding_layout.buffer.hasDynamicOffset = true;
    // The texture binding
    BindGroupLayoutEntry& texture_binding_layout = binding_layout_entries[1];
    texture_binding_layout.binding = 1;
    texture_binding_layout.visibility = ShaderStage::Fragment;
    texture_binding_layout.texture.sampleType = TextureSampleType::Float;
    texture_binding_layout.texture.viewDimension = TextureViewDimension::_2D;
    // The texture sampler binding
    BindGroupLayoutEntry& samplerBindingLayout = binding_layout_entries[2];
    samplerBindingLayout.binding = 2;
    samplerBindingLayout.visibility = ShaderStage::Fragment;
    samplerBindingLayout.sampler.type = SamplerBindingType::Filtering;

    // Create a bind group layout
    bind_group_layout_desc.entryCount = (uint32_t)binding_layout_entries.size();
    bind_group_layout_desc.entries = binding_layout_entries.data();
    bind_group_layout = device.createBindGroupLayout(bind_group_layout_desc);

    // Create the pipeline layout
    PipelineLayoutDescriptor layout_desc;
    layout_desc.bindGroupLayoutCount = 1;
    layout_desc.bindGroupLayouts = (WGPUBindGroupLayout*)&bind_group_layout;
    PipelineLayout layout = device.createPipelineLayout(layout_desc);
    pipeline_desc.layout = layout;

    pipeline = device.createRenderPipeline(pipeline_desc);
    assert(pipeline && "CRITICAL: Pipeline failed to Create");

    // We no longer need to access the shader module
    shader_module.release();

    // Create the depth texture
    TextureDescriptor depth_texture_desc;
    depth_texture_desc.dimension = TextureDimension::_2D;
    depth_texture_desc.format = depth_texture_format;
    depth_texture_desc.mipLevelCount = 1;
    depth_texture_desc.sampleCount = 1;
    depth_texture_desc.size = {WIN_WIDTH, WIN_HEIGHT, 1};
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

    // Create a sampler
    SamplerDescriptor sampler_desc;
    sampler_desc.addressModeU = AddressMode::Repeat;
    sampler_desc.addressModeV = AddressMode::Repeat;
    sampler_desc.addressModeW = AddressMode::ClampToEdge;
    sampler_desc.magFilter = FilterMode::Linear;
    sampler_desc.minFilter = FilterMode::Linear;
    sampler_desc.mipmapFilter = MipmapFilterMode::Linear;
    sampler_desc.lodMinClamp = 0.0f;
    sampler_desc.lodMaxClamp = 8.0f;
    sampler_desc.compare = CompareFunction::Undefined;
    sampler_desc.maxAnisotropy = 1;
    sampler = device.createSampler(sampler_desc);

    std::cout << "Pipeline Initialized!" << std::endl;
}