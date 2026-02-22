#pragma once

#include <expected>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

#include "VanK/Renderer/Shader.h"

#include "VulkanRendererAPI.h"

namespace VanK
{
    struct ShaderModuleInfo;
    struct ShaderStageInfo;

    class VulkanShader : public Shader
    {
    public:
        VulkanShader(const std::string& filePath);
        virtual ~VulkanShader() override;

        virtual void Bind() const override;
        virtual void Unbind() const override;

        vk::raii::ShaderModule& GetShaderModule(vk::ShaderStageFlagBits stage, size_t index = 0);
        std::string GetShaderEntryName(vk::ShaderStageFlagBits stage, size_t index = 0) const;
        bool HasStage(vk::ShaderStageFlagBits stage , size_t index = 0) const;
        virtual const std::string& GetName() const override { return m_Name; };
        const std::string& GetFilePath() const override { return m_FilePath; }

    private:
        std::unordered_map<vk::ShaderStageFlagBits, std::vector<ShaderStageInfo>> loadCachedSpv(
            std::vector<std::string> EntryPoints, std::string cachePath, std::unordered_map<vk::ShaderStageFlagBits,
            std::vector<ShaderStageInfo>> spirvPerStage);
        std::expected<std::unordered_map<vk::ShaderStageFlagBits, std::vector<ShaderStageInfo>>, std::string> compileSlang();
        void Compile(const std::unordered_map<vk::ShaderStageFlagBits, std::vector<ShaderStageInfo>>& shaderSources);
    private:
        uint32_t m_RendererID;
        std::string m_Name;
        std::string m_FilePath;
        std::unordered_map<vk::ShaderStageFlagBits, std::vector<ShaderModuleInfo>> m_ShaderModules;
    };
}