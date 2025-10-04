#include "Shader.h"

#include <iostream>

#include "RendererAPI.h"
#include "Platform/Vulkan/VulkanShader.h"

namespace VanK
{
    Shader* Shader::Create(const std::string& filepath)
    {
        switch (RendererAPI::GetAPI())
        {
        case RenderAPIType::None: return nullptr;
        case RenderAPIType::Vulkan: return new VulkanShader(filepath);
        }

        return nullptr;
    }

    void ShaderLibrary::Add(const std::string& name, std::unique_ptr<Shader> shader)
    {
        if (Exists(name))
        {
            std::cerr << "Warning: Shader '" << name << "' already exists. Overwriting.\n";
        }
        m_Shaders[name] = std::move(shader);
    }

    Shader* ShaderLibrary::Load(const std::string& name, const std::string& filepath)
    {
        std::unique_ptr<Shader> shader(Shader::Create(filepath));
        Shader* raw = shader.get();
        Add(name, std::move(shader));
        return raw;
    }

    Shader* ShaderLibrary::Get(const std::string& name)
    {
        return m_Shaders.at(name).get();
    }

    bool ShaderLibrary::Exists(const std::string& name) const
    {
        return m_Shaders.find(name) != m_Shaders.end();
    }

    void ShaderLibrary::Remove(const std::string& name)
    {
        auto it = m_Shaders.find(name);
        if (it != m_Shaders.end())
        {
            m_Shaders.erase(it); // This deletes the unique_ptr and frees the shader
        }
        else
        {
            std::cerr << "Warning: Shader '" << name << "' does not exist.\n";
        }
    }
    
    void ShaderLibrary::ShutdownAll()
    {
        m_Shaders.clear();
    }

    std::vector<std::string> ShaderLibrary::GetAllShaderPaths() const
    {
        std::vector<std::string> paths;
        for (const auto& [name, shader] : m_Shaders)
        {
            if (shader)
            {
                std::cout << "[ShaderLibrary] Shader '" << name << "' path: " << shader->GetFilePath() << std::endl;
                paths.push_back(shader->GetFilePath());
            }
        }
        return paths;
    }
}