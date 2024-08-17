#pragma once

#include <webgpu/webgpu.hpp>

#include <vector>
#include <filesystem>

class ResourceManager
{
  public:
    using path = std::filesystem::path;
    
    //A structure that describes the data layout in the vertex buffer,
    //used by load_geometry_from_obj and used it in `sizeof` and `offsetof`
    //when uploading data to the GPU.
    struct VertexAttributes
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec3 color;
        glm::vec2 uv;
    };

    // Load a shader from a WGSL file into a new shader module
    static wgpu::ShaderModule load_shader_module(const path& path, wgpu::Device device);

    // Load an 3D mesh from a standard .obj file into a vertex data buffer
    static bool load_geometry_from_obj(const path& path, std::vector<VertexAttributes>& vertexData);

    // Load an image from a standard image file into a new texture object
    // NB: The texture must be destroyed after use
    static wgpu::Texture load_texture(const path& path, wgpu::Device device, wgpu::TextureView* pTextureView = nullptr);
};