#include "VulkanShader.h"

#include <print>
#include <expected>
#include <filesystem>
#include <utility>
#include <functional>
#include "VanK/Renderer/Shader.h"

#include "VulkanRendererAPI.h"
#include "VanK/Utils.h"

#include "slang.h"
#include "slang-com-ptr.h"
#include "slang-com-helper.h"
#include "VanK/Core/Application.h"

namespace VanK
{
    struct ShaderStageInfo
    {
        std::string entryPointName;
        std::vector<uint32_t> spirvCode;
    };
    
    void diagnoseIfNeeded(slang::IBlob* diagnosticsBlob)
    {
        if (diagnosticsBlob != nullptr)
        {
            std::cout << static_cast<const char*>(diagnosticsBlob->getBufferPointer()) << std::endl;
        }
    }

    std::unordered_map<vk::ShaderStageFlagBits, ShaderStageInfo> VulkanShader::loadCachedSpv
    (
        std::vector<std::string> EntryPoints, std::string cachePath,
        std::unordered_map<vk::ShaderStageFlagBits, ShaderStageInfo> spirvPerStage
    )
    {
        for (std::string entryPoint : EntryPoints)
        {
            // Map known entry points to Vulkan shader stages
            vk::ShaderStageFlagBits stage;
            if (entryPoint == "vertexMain")         stage = vk::ShaderStageFlagBits::eVertex;
            else if (entryPoint == "fragmentMain")  stage = vk::ShaderStageFlagBits::eFragment;
            else if (entryPoint == "compMain")          stage = vk::ShaderStageFlagBits::eCompute;
            else continue;
                
            std::string fileName = m_Name + "." + entryPoint;
            std::string fullPath = cachePath + fileName + ".spv";
            // Check existence first
            if (!std::filesystem::exists(fullPath))
            {
                std::cout << "[Hash] File '" << fullPath << "' doesn't exist." << std::endl;
                continue;  
            }
                
            auto data = Utility::LoadSpvFromPath(fullPath);
            if (data.empty()) continue;
            spirvPerStage[stage] = ShaderStageInfo{entryPoint, std::move(data)};
        }
        return spirvPerStage;
    }
    
    vk::ShaderStageFlagBits mapEntryToStage(const std::string& entry)
    {
        if (entry == "vertexMain")   return vk::ShaderStageFlagBits::eVertex;
        if (entry == "fragmentMain") return vk::ShaderStageFlagBits::eFragment;
        if (entry == "compMain")         return vk::ShaderStageFlagBits::eCompute;
        throw std::runtime_error("Unknown entry point: " + entry);
    }
    
    std::expected<std::unordered_map<vk::ShaderStageFlagBits, ShaderStageInfo>, std::string> VulkanShader::compileSlang()
    {
        bool forceCompile = false;
        // All EntryPoints possible
        std::vector<std::string> EntryPoints
        {
            "vertexMain",
            "fragmentMain",
            "compMain", // maybe i can call this computemain ? will see
        };

        std::unordered_map<vk::ShaderStageFlagBits, ShaderStageInfo> spirvPerStage;

        std::string cachePath = Utility::GetCachePath();

        std::string fileHashName = m_Name; // shader.CircleComp
        XXH128_hash_t currentHash = Utility::calcul_hash_streaming(m_FilePath); // ../../../VanK-Editor/assets/shaders/shader.CircleComp.slang
        XXH128_hash_t cachedHash{};
        std::string hashFile = cachePath + fileHashName + ".hash";
        bool hashMatches = Utility::loadHashFromFile(hashFile, cachedHash) && (cachedHash.low64 == currentHash.low64 && cachedHash.high64 == currentHash.high64);
        std::cout << "[Hash] Current: " << std::hex << currentHash.high64 << currentHash.low64 << '\n';
        std::cout << "[Hash] Cached : " << std::hex << cachedHash.high64 << cachedHash.low64 << '\n';

        if (hashMatches && !forceCompile)
        {
            spirvPerStage.clear();
            return loadCachedSpv(EntryPoints, cachePath, spirvPerStage);
        }
        
        std::filesystem::path shaderFilePath = std::filesystem::path(m_FilePath).make_preferred();

        // First we need to create slang global session with work with the Slang API.
        Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;
        if (SLANG_FAILED(slang::createGlobalSession(slangGlobalSession.writeRef())))
        {
            return std::unexpected<std::string>("Slang Failed to create Global Session");
        }

        // Next we create a compilation session to generate SPIRV code from Slang source.
        slang::SessionDesc sessionDesc = {};
        slang::TargetDesc targetDesc = {};
        targetDesc.format = SLANG_SPIRV;
        targetDesc.profile = slangGlobalSession->findProfile("spirv_1_5");
        targetDesc.flags = 0;

        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;

        std::array<slang::CompilerOptionEntry, 3> options = 
        {
            {
                slang::CompilerOptionName::GLSLForceScalarLayout,
                { slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr },
                
                slang::CompilerOptionName::EmitSpirvDirectly,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr},
            
                slang::CompilerOptionName::VulkanUseEntryPointName,
                { slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr },
            }
        };
        sessionDesc.compilerOptionEntries = options.data();
        sessionDesc.compilerOptionEntryCount = options.size();

        Slang::ComPtr<slang::ISession> session;
        if (SLANG_FAILED(slangGlobalSession->createSession(sessionDesc, session.writeRef())))
        {
            return std::unexpected<std::string>("Slang Failed to create Session");
        }

        slang::IModule* slangModule = nullptr;
        {
            Slang::ComPtr<slang::IBlob> diagnosticBlob;
            std::string path = shaderFilePath.string();
            slangModule = session->loadModule(path.c_str(), diagnosticBlob.writeRef());
            diagnoseIfNeeded(diagnosticBlob);
            if (!slangModule)
            {
                spirvPerStage.clear();
                return loadCachedSpv(EntryPoints, cachePath, spirvPerStage);
            }
        }

        for (auto& entry : EntryPoints)
        {
            Slang::ComPtr<slang::IEntryPoint> entryPoint;
            slangModule->findEntryPointByName(entry.c_str(), entryPoint.writeRef());
            if (!entryPoint)
            {
                std::cout << "[Slang] Entry point '" << entry << "' not found in module '" << m_Name << "'\n";
                continue;
            }

            std::vector<slang::IComponentType*> componentTypes;
            componentTypes.emplace_back(slangModule);
            componentTypes.emplace_back(entryPoint);

            Slang::ComPtr<slang::IComponentType> composedProgram;
            {
                Slang:Slang::ComPtr<slang::IBlob> diagnosticBlob;
                SlangResult result = session->createCompositeComponentType
                (
                    componentTypes.data(),
                    componentTypes.size(),
                    composedProgram.writeRef(),
                    diagnosticBlob.writeRef()
                );
                diagnoseIfNeeded(diagnosticBlob);
                if (SLANG_FAILED(result))
                    return std::unexpected<std::string>("Slang operation composedProgram failed with code: " + std::to_string(result));
            }

            Slang::ComPtr<slang::IBlob> spirvCode;
            {
                Slang::ComPtr<slang::IBlob> diagnosticBlob;
                SlangResult result = composedProgram->getEntryPointCode
                (
                    0,
                    0,
                    spirvCode.writeRef(),
                    diagnosticBlob.writeRef()
                );
                diagnoseIfNeeded(diagnosticBlob);
                if (SLANG_FAILED(result))
                {
                    std::cout << "[Slang] Failed to spirvCode from composedProgram for entry: " << entry << "\n";
                    continue;
                }
            }

            // converting to usable byte code and saving cache/hash
            std::vector<uint32_t> spirvCodeToUint32;
            {
                auto byteSize = spirvCode->getBufferSize();
                auto ptr = static_cast<const uint32_t*>(spirvCode->getBufferPointer());
                spirvCodeToUint32.assign(ptr, ptr + byteSize / sizeof(uint32_t));
            }

            std::string fileName = m_Name + "." + entry;;
            std::string fullPath = cachePath + fileName + ".spv";
            
            Utility::SaveToFile(fullPath.c_str(), spirvCodeToUint32.data(), spirvCodeToUint32.size() * sizeof(uint32_t));
            
            Utility::saveHashToFile(hashFile, currentHash);

            spirvPerStage[mapEntryToStage(entry)] = ShaderStageInfo{entry, spirvCodeToUint32};
        }
        
        return spirvPerStage;
    }
    
    struct ShaderModuleInfo {
        vk::raii::ShaderModule module;
        std::string entryPointName;
    };
    
    void VulkanShader::Compile(const std::unordered_map<vk::ShaderStageFlagBits, ShaderStageInfo>& shaderSources)
    {
        std::cout << "Compile called with " << shaderSources.size() << " shader stages\n";
        auto& instance = VulkanRendererAPI::Get();
        vk::raii::Device& device = instance.GetDevice();
        for (const auto& [stage, spirv] : shaderSources)
        {
            std::cout << "entrypouint " << spirv.entryPointName << " module " << spirv.spirvCode.data() << std::endl;
            vk::raii::ShaderModule shaderModule = utils::createShaderModule(device, std::span<const uint32_t>(spirv.spirvCode.data(), spirv.spirvCode.size()));
            DBG_VK_NAME(*shaderModule);
            m_ShaderModules.emplace(stage, ShaderModuleInfo{ std::move(shaderModule), spirv.entryPointName });
        }
        //maybe in the future store it in here and then only that specific shader has the correct stuff it overwrites grapgics because compute is last fixed inside sershadermodule
    }

    VulkanShader::VulkanShader(const std::string& fileName)
    {
        std::string rootPath = Application::Get().GetExecutableRootPath();
        const std::vector<std::string> searchPaths =
        {
            rootPath + "../../VanK/shaders"
        };

        std::string shaderFile = utils::findFile(fileName, searchPaths);
        std::cout << "Shader file: " << shaderFile << '\n';

        m_FilePath = shaderFile;

        //get from filepath the name of the file maybe sdl3 can do that autoamticly Extract name from filepath
        auto lastSlash = shaderFile.find_last_of("/\\");
        lastSlash = lastSlash == std::string::npos ? 0 : lastSlash + 1;
        auto lastDot = shaderFile.rfind('.');
        auto count = lastDot == std::string::npos ? shaderFile.size() - lastSlash : lastDot - lastSlash;
        m_Name = shaderFile.substr(lastSlash, count);
        std::cout << "Shader name: " << m_Name << '\n';

        //error handling here
        Compile(compileSlang().value());
    }

    VulkanShader::~VulkanShader()
    {
        std::cout << "Shader destroyed: " << m_Name << '\n';

        m_ShaderModules.clear();
    }

    vk::raii::ShaderModule& VulkanShader::GetShaderModule(vk::ShaderStageFlagBits stage)
    {
        auto it = m_ShaderModules.find(stage);
        if (it != m_ShaderModules.end())
            return it->second.module;

        throw std::runtime_error("Shader module not found");
    }

    std::string VulkanShader::GetShaderEntryName(vk::ShaderStageFlagBits stage) const
    {
        auto it = m_ShaderModules.find(stage);
        if (it != m_ShaderModules.end())
            return it->second.entryPointName;

        return "";
    }

    void VulkanShader::Bind() const
    {
    }

    void VulkanShader::Unbind() const
    {
    }
}
