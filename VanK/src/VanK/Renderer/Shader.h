#pragma once
#include <string>
#include <unordered_map>
#include <memory>  // <-- required for unique_ptr
namespace VanK
{
    class Shader
    {
    public:
        virtual ~Shader() = default;

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;

        virtual const std::string& GetName() const = 0;
        virtual const std::string& GetFilePath() const = 0;
    
        static Shader* Create(const std::string& filepath);
    };

    class ShaderLibrary
    {
    public:
        void Add(const std::string& name, std::unique_ptr<Shader> shader);
        Shader* Load(const std::string& name, const std::string& filepath);
        Shader* Get(const std::string& name);
        bool Exists(const std::string& name) const;
        void Remove(const std::string& name);
        void ShutdownAll();
        std::vector<std::string> GetAllShaderPaths() const;

    private:
        std::unordered_map<std::string, std::unique_ptr<Shader>> m_Shaders;
    };
}