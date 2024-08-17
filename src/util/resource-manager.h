#pragma once

#include <webgpu/webgpu.hpp>

#include <vector>
#include <filesystem>

class ResourceManager
{
  public:
    using path = std::filesystem::path;
    
    //A structure that describes the data layout in the vertex buffer,
    //used by loadGeometryFromObj and used it in `sizeof` and `offsetof`
    //when uploading data to the GPU.
    struct VertexAttributes
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec3 color;
        glm::vec2 uv;
    };

    // Load a shader from a WGSL file into a new shader module
    static wgpu::ShaderModule loadShaderModule(const path& path, wgpu::Device device);

    // Load an 3D mesh from a standard .obj file into a vertex data buffer
    static bool loadGeometryFromObj(const path& path, std::vector<VertexAttributes>& vertexData);

    // Load an image from a standard image file into a new texture object
    // NB: The texture must be destroyed after use
    static wgpu::Texture loadTexture(const path& path, wgpu::Device device, wgpu::TextureView* pTextureView = nullptr);
};