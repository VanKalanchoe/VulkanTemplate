# pragma once

#include <string>
#include <filesystem>

#include "VanK/Core/core.h"

#include "VanK/Asset/RuntimeAssetManager.h"
#include "VanK/Asset/EditorAssetManager.h"


namespace VanK
{
    struct ProjectConfig
    {
        std::string Name = "Untitled";

        AssetHandle StartScene;

        std::filesystem::path AssetDirectory;
        std::filesystem::path AssetRegistryPath; // Relative to AssetDirectory
        std::filesystem::path ScriptModulePath;
    };
    
    class Project
    {
    public:
        static const std::filesystem::path& GetProjectDirectory()
        {
            VK_CORE_ASSERT(s_ActiveProject, "No active project");
            return s_ActiveProject->m_ProjectDirectory;
        }
        
        static std::filesystem::path GetAssetDirectory()
        {
            VK_CORE_ASSERT(s_ActiveProject, "No active project");
            return GetProjectDirectory() / s_ActiveProject->m_Config.AssetDirectory;
        }

        static std::filesystem::path GetAssetRegistryPath()
        {
            VK_CORE_ASSERT(s_ActiveProject, "No active project");
            return GetAssetDirectory() / s_ActiveProject->m_Config.AssetRegistryPath;
        }
        
        // todo move to asset manager when we have one
        static std::filesystem::path GetAssetFileSystemPath(const std::filesystem::path& path)
        {
            VK_CORE_ASSERT(s_ActiveProject, "No active project");
            return GetAssetDirectory() / path;
        }

        std::filesystem::path GetAssetAbsolutePath(const std::filesystem::path& path);

        static const std::filesystem::path& GetActiveProjectDirectory()
        {
            VK_CORE_ASSERT(s_ActiveProject);
            return s_ActiveProject->GetProjectDirectory();
        }

        static std::filesystem::path GetActiveAssetDirectory()
        {
            VK_CORE_ASSERT(s_ActiveProject);
            return s_ActiveProject->GetAssetDirectory();
        }

        static std::filesystem::path GetActiveAssetRegistryPath()
        {
            VK_CORE_ASSERT(s_ActiveProject);
            return s_ActiveProject->GetAssetRegistryPath();
        }

        // TODO(Yan): move to asset manager when we have one
        static std::filesystem::path GetActiveAssetFileSystemPath(const std::filesystem::path& path)
        {
            VK_CORE_ASSERT(s_ActiveProject);
            return s_ActiveProject->GetAssetFileSystemPath(path);
        }
        
        ProjectConfig& GetConfig() { return m_Config; }

        static Ref<Project> GetActive() { return s_ActiveProject; }
        std::shared_ptr<AssetManagerBase> GetAssetManager() { return m_AssetManager; }
        std::shared_ptr<RuntimeAssetManager> GetRuntimeAssetManager() { return std::static_pointer_cast<RuntimeAssetManager>(m_AssetManager); }
        std::shared_ptr<EditorAssetManager> GetEditorAssetManager() { return std::static_pointer_cast<EditorAssetManager>(m_AssetManager); }

        static Ref<Project> New();
        static Ref<Project> Load(const std::filesystem::path& path);
        static bool SaveActive(const std::filesystem::path& path);

    private:
        ProjectConfig m_Config;
        std::filesystem::path m_ProjectDirectory;
        std::shared_ptr<AssetManagerBase> m_AssetManager;

        inline static Ref<Project> s_ActiveProject;
    };
}
