#include "resource-manager.h"

#include "stb_image.h"
#include "tiny_obj_loader.h"

#include <fstream>
#include <cstring>

using namespace wgpu;

ShaderModule ResourceManager::load_shader_module(const path& path, Device device)
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

    ShaderModuleWGSLDescriptor shader_code_desc;
    shader_code_desc.chain.next = nullptr;
    shader_code_desc.chain.sType = SType::ShaderModuleWGSLDescriptor;
    shader_code_desc.code = shader_source.c_str();
    ShaderModuleDescriptor shader_desc;
    shader_desc.nextInChain = &shader_code_desc.chain;
#ifdef WEBGPU_BACKEND_WGPU
    shader_desc.hintCount = 0;
    shader_desc.hints = nullptr;
#endif

    return device.createShaderModule(shader_desc);
}

bool ResourceManager::load_geometry_from_obj(const path& path, std::vector<VertexAttributes>& vertexData)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;

    // Call the core loading procedure of TinyOBJLoader
    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.string().c_str());

    // Check errors
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
    vertexData.clear();
    for (const auto& shape : shapes)
    {
        size_t offset = vertexData.size();
        vertexData.resize(offset + shape.mesh.indices.size());

        for (size_t i = 0; i < shape.mesh.indices.size(); ++i)
        {
            const tinyobj::index_t& idx = shape.mesh.indices[i];

            vertexData[offset + i].position = {attrib.vertices[3 * idx.vertex_index + 0], -attrib.vertices[3 * idx.vertex_index + 2],
                                               attrib.vertices[3 * idx.vertex_index + 1]};

            vertexData[offset + i].normal = {attrib.normals[3 * idx.normal_index + 0], -attrib.normals[3 * idx.normal_index + 2],
                                             attrib.normals[3 * idx.normal_index + 1]};

            vertexData[offset + i].color = {attrib.colors[3 * idx.vertex_index + 0], attrib.colors[3 * idx.vertex_index + 1],
                                            attrib.colors[3 * idx.vertex_index + 2]};

            vertexData[offset + i].uv = {attrib.texcoords[2 * idx.texcoord_index + 0], 1 - attrib.texcoords[2 * idx.texcoord_index + 1]};
        }
    }

    return true;
}

// Auxiliary function for load_texture
static void write_mip_maps(Device device, Texture texture, Extent3D texture_size, uint32_t mip_level_count, const unsigned char* pixel_data)
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

// Equivalent of std::bit_width that is available from C++20 onward
static uint32_t bit_width(uint32_t m)
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

Texture ResourceManager::load_texture(const path& path, Device device, TextureView* texture_view)
{
    int width, height, channels;
    unsigned char* pixel_data = stbi_load(path.string().c_str(), &width, &height, &channels, 4 /* force 4 channels */);
    // If data is null, loading failed.
    if (nullptr == pixel_data)
        return nullptr;

    // Use the width, height, channels and data variables here
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

    // Upload data to the GPU texture
    write_mip_maps(device, texture, texture_desc.size, texture_desc.mipLevelCount, pixel_data);

    stbi_image_free(pixel_data);
    // (Do not use data after this)

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

    return texture;
}