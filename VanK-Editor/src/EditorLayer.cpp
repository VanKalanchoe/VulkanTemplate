#include "EditorLayer.h"

#include <print>

#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>  // for pointer to matrix or vector

#include "VanK/Core/VanK.h"
#include "VanK/Renderer/Renderer.h"
#include "VanK/Math/Math.h"
#include "VanK/Utils.h"
#include "VanK/Asset/AssetManager.h"
#include "VanK/Asset/SceneImporter.h"
#include "VanK/Asset/TextureImporter.h"
#include "VanK/Core/Timer.h"
#include "VanK/ImGui/ImGuiLayer.h"

namespace VanK
{
    static Ref<Font> s_Font;

    EditorLayer::EditorLayer() : Layer("EditorLayer"), m_CameraController(1280.0f / 720.0f)
    {
        s_Font = Font::GetDefault(); //cant use this ebcause static otherwise it dies last even last to renderer works again ebcasue i remove static in shutdown vulkanrenderapi
        // ui toolbar disabled beider panels.onimguirender disabled renderer drawframe submit rendering zu endsubmit gezogen
        // pngs zu ktx konvertien mit meinem python program // enable all pngs back fix texture class something still messed
        //create texture still running inside vulanrendererapi
        m_IconPlay = TextureImporter::LoadTexture2D("../build/VanK-Editor/Resources/Icons/PlayButton.ktx2", {.GenerateMips = false});
        m_IconStop = TextureImporter::LoadTexture2D("../build/VanK-Editor/Resources/Icons/StopButton.ktx2", {.GenerateMips = false});
        m_IconPause = TextureImporter::LoadTexture2D("../build/VanK-Editor/Resources/Icons/PauseButton.ktx2", {.GenerateMips = false});
        m_IconSimulate = TextureImporter::LoadTexture2D("../build/VanK-Editor/Resources/Icons/SimulateButton.ktx2", {.GenerateMips = false});
        m_IconStep = TextureImporter::LoadTexture2D("../build/VanK-Editor/Resources/Icons/StepButton.ktx2", {.GenerateMips = false});

        m_EditorScene = CreateRef<Scene>();
        m_ActiveScene = m_EditorScene;

        auto commandLineArgs = Application::Get().GetSpecification().CommandLineArgs;
        if (commandLineArgs.Count > 1)
        {
            std::cout << "Loading Project: " << commandLineArgs[1] << std::endl;
            auto projectFilePath = commandLineArgs[1];
            OpenProject(projectFilePath);
        }
        else
        {
            // TODO: promp the user to select a directory
            //NewProject();

            // If no project is opened, close vank
            // note: this is while we dont have a new project path
            if (!OpenProject())
            {
                SDL_Event event;
                event.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&event);
            }
        }

        m_EditorCamera = EditorCamera(30.0f, 1.778f, 0.1f, 1000.0f);
    }

    EditorLayer::~EditorLayer()
    {
        std::cout << "EditorLayer::~EditorLayer()" << std::endl;
    }

    void EditorLayer::OnEvent(VanK::Event& event)
    {
        //std::println("{}", event.ToString());

        m_CameraController.OnEvent(event);

        if (m_SceneState == SceneState::Edit)
            m_EditorCamera.OnEvent(event);

        VanK::EventDispatcher dispatcher(event);
        dispatcher.Dispatch<VanK::KeyPressedEvent>([this](VanK::KeyPressedEvent& e) { return OnKeyPressed(e); });
        dispatcher.Dispatch<VanK::MouseButtonPressedEvent>([this](VanK::MouseButtonPressedEvent& e) { return OnMouseButtonPressed(e); });
    }

    void EditorLayer::OnUpdate(Timestep ts)
    {
        // Query current window size
        if (!Renderer::GetIsEditor())
        {
            int width, height;
            SDL_GetWindowSize(Application::Get().getWindow()->getWindowHandle(), &width, &height);

            m_ViewportSize =
            {
                std::max(1u, static_cast<uint32_t>(width)),
                std::max(1u, static_cast<uint32_t>(height))
            };
        }

        m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

        if (m_ViewportSize.x != lastViewportExtent.x || m_ViewportSize.y != lastViewportExtent.y)
        {
            std::cout << "Viewport size changed: " << m_ViewportSize.x << ", " << m_ViewportSize.y << std::endl;

            lastViewportExtent.x = m_ViewportSize.x;
            lastViewportExtent.y = m_ViewportSize.y;

            Renderer::SetViewportSize({m_ViewportSize.x, m_ViewportSize.y});
            m_CameraController.OnResize(m_ViewportSize.x, m_ViewportSize.y);
            m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
        }

        /*Renderer::ResetStats();*/

        switch (m_SceneState)
        {
        case SceneState::Edit:
            {
                if (m_ViewportFocused)
                {
                    m_CameraController.OnUpdate(ts);
                }

                m_EditorCamera.OnUpdate(ts);

                m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera);
                break;
            }
        case SceneState::Simulate:
            {
                m_EditorCamera.OnUpdate(ts);

                m_ActiveScene->OnUpdateSimulation(ts, m_EditorCamera);
                break;
            }
        case SceneState::Play:
            {
                m_ActiveScene->OnUpdateRuntime(ts);
                break;
            }
        }

        // Mouse Selection
        auto [mx, my] = ImGui::GetMousePos();
        mx -= m_ViewportBounds[0].x;
        my -= m_ViewportBounds[0].y;
        glm::vec2 viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
        //my = viewportSize.y - my;
        int mouseX = (int)mx;
        int mouseY = (int)my;

        if (mouseX >= 0 && mouseY >= 0 && mouseX < (int)viewportSize.x && mouseY < (int)viewportSize.y)
        {
            /*ScopeTimer timer("MousePicking");*/
            // Retrieve the pixel data (ID) from the calculated index
            // reading only 1 pixel right now but if multi select maybe i need full viewport ? 
            int pixelData = RenderCommand::ReadEntityIDAtPixel(Renderer::entityImage->GetRenderImageIndex(), mouseX, mouseY); // chnage this

            m_HoveredEntity = pixelData == -1 ? Entity() : Entity((entt::entity)pixelData, m_ActiveScene.get());
        }

        OnOverlayRender();
    }

    void EditorLayer::OnRender()
    {
        /*Renderer::Flush();*/
    }

    void EditorLayer::OnImGuiRender()
    {
        // Setup Docking

        static bool opt_fullscreen = true;
        static bool opt_padding = false;
        //static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
        static bool dockspaceOpen = true;

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;;

        if (opt_fullscreen)
        {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }

        if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
            window_flags |= ImGuiWindowFlags_NoBackground;

        if (!opt_padding)
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        // Start the main DockSpace window
        ImGui::Begin("DockSpace", &dockspaceOpen, window_flags);

        if (!opt_padding)
            ImGui::PopStyleVar(); // Restore padding style

        if (opt_fullscreen)
            ImGui::PopStyleVar(2); // Restore style settings for fullscreen

        // DockSpace
        ImGuiIO& io = ImGui::GetIO();
        ImGuiStyle& style = ImGui::GetStyle();
        float minWinSizeX = style.WindowMinSize.x;
        style.WindowMinSize.x = 370.0f;

        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }

        style.WindowMinSize.x = minWinSizeX;

        // Menu Bar for additional options

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open Project...", "Ctrl+O"))
                {
                    OpenProject();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("New Scene", "Ctrl+N"))
                {
                    NewScene();
                }

                if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                {
                    SaveScene();
                }

                if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
                {
                    SaveSceneAs();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Exit")) Application::Shutdown(); // idk maybe use bool or so
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Script"))
            {
                if (ImGui::MenuItem("Reload assembly", "Ctrl+R"))
                {
                    /*ScriptEngine::ReloadAssembly();*/ // otherwise it thinkgs its exectuing endmenu if you dont use {}
                }

                ImGui::EndMenu();
            }

            ImGui::SameLine();
            static bool vsync = Renderer::GetVSync();
            if (ImGui::Checkbox("Vsync", &vsync))
            {
                Renderer::QueVSyncChange(vsync);
            }

            ImGui::SameLine();
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

            ImGui::EndMenuBar();
        }

        m_SceneHierarchyPanel.OnImGuiRender();
        m_ContentBrowserPanel->OnImGuiRender();

        // "Right" Window
        ImGui::Begin("Stats");

        std::string name = "None";
        if (m_HoveredEntity && m_HoveredEntity.HasComponent<TagComponent>())
        {
            name = m_HoveredEntity.GetComponent<TagComponent>().Tag;
        }
        ImGui::Text("Hovered Entity: %s", name.c_str());

        ImGui::Spacing();

        /*auto stats3D = Renderer::GetStats();
        ImGui::Text("Renderer3D stats:");
        ImGui::Text("Draw Calls: %d", stats3D.DrawCalls);
        ImGui::Text("Cubes: %d", stats3D.CubeCount);
        ImGui::Text("CubeVertices: %d", stats3D.GetTotalVertexCount());
        ImGui::Text("CubeIndices: %d", stats3D.GetTotalIndexCount());
        ImGui::Spacing();
        ImGui::Text("MeshCount: %d", stats3D.MeshCount);
        ImGui::Text("MeshVertices: %d", stats3D.GetTotalMeshVertexCount());
        ImGui::Text("MeshIndices: %d", stats3D.GetTotalMeshIndexCount());*/

        ImGui::Spacing();

        /*auto stats = Renderer2D::GetStats();
        ImGui::Text("Renderer2D stats:");
        ImGui::Text("Draw Calls: %d", stats.DrawCalls);
        ImGui::Text("Quads : %d", stats.QuadCount);
        ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
        ImGui::Text("Indices: %d", stats.GetTotalIndexCount());
        ImVec2 right = ImGui::GetWindowSize();
        ImGui::Text("Window Size: %.0fx%.0f", right.x, right.y);*/

        ImGui::Text("ImGui ActiveID: %u", Application::Get().GetLayer<ImGuiLayer>()->GetActiveWidgetID());
        ImGui::End(); // End "right" Window

        ImGui::Spacing();

        ImGui::Begin("Renderer");
        /*  VanKDeviceProperties properties = RenderCommand::GetDeviceProperties();
          ImGui::Text("DeviceName: %s", properties.deviceName.c_str());
          ImGui::Text("Nvidia Driver Version: %u.%u.%u", properties.driverMajor, properties.driverMinor, properties.driverPatch);
          ImGui::Text("Vulkan API Version: %u.%u.%u", properties.apiMajor, properties.apiMinor, properties.apiPatch);
      
          ImGui::Spacing();
      */
        
        // gpu total time
        RenderCommand::setEnableTimeStamp(true);
        auto gpu = RenderCommand::getGPURenderTime();
        float gpuMs = static_cast<float>(gpu.end - gpu.begin) * RenderCommand::getTimeStampPeriod() * 1e-6f;
        ImGui::Text("(GPU: %.3f ms)", gpuMs);
        
        /*auto gpu2 = Renderer::lol;
        float gpuMs2 = static_cast<float>(gpu2.end - gpu2.begin) * RenderCommand::getTimeStampPeriod() * 1e-6f;
        ImGui::Text("(GPU: %.3f ms)", gpuMs2);*/

        for (auto& [profileName, profileTime] : g_GPUProfileResults)
        {
            float gpuMsTime = static_cast<float>(profileTime.end - profileTime.begin) * RenderCommand::getTimeStampPeriod() * 1e-6f;
            ImGui::Text("%s: %.3f ms", profileName.c_str(), gpuMsTime);
        }
        
        ImGui::Spacing();

        for (auto& [profileName, profileTime] : g_CPUProfileResults)
            ImGui::Text("%s: %.3f ms", profileName.c_str(), profileTime);
        
        ImGui::End();

        ImGui::Begin("Settings");
        
        ImGui::Checkbox("Show physics collider", &m_ShowPhysicsColliders);
        
        bool frustumToggle = Renderer::isFrustumCullEnabled();
        ImGui::Checkbox("Frustum Cull", &frustumToggle);
        Renderer::SetFrustumCullEnabled(frustumToggle);
        
        ImGui::Text("Primitives: %llu", RenderCommand::getPipelineStatistics().clippingInvocations);
        
        int lineWidth = static_cast<int>(Renderer::GetLineWidth());
        if (ImGui::SliderInt("Line Width", &lineWidth, 1.0f, 10.0f))
        {
            Renderer::SetLineWidth(static_cast<float>(lineWidth));
        }
        
        const char* cullModes[] = { "None", "Back", "Front" };
        static int currentCull = Renderer::GetCullMode(); // 0=None, 1=Back, 2=Front
        if (ImGui::Combo("Cull Mode", &currentCull, cullModes, IM_ARRAYSIZE(cullModes)))
        {
            switch (currentCull)
            {
            case 0 : Renderer::SetCullMode(VanK_CULL_MODE_NONE); break;
            case 1 : Renderer::SetCullMode(VanK_CULL_MODE_BACK_BIT); break;
            case 2 : Renderer::SetCullMode(VanK_CULL_MODE_FRONT_BIT); break;
            }
        }
        
        ImGui::Image(s_Font->GetAtlasTexture()->getImTextureID(), {512, 512}, ImVec2(0, 1), ImVec2(1, 0));
        
        ImGui::End(); // End Settings

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
        ImGui::Begin("Viewport");

        //Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportHovered); //do this dont forget
        if (m_ViewportHovered)
        {
            Application::Get().BlockEvents(false);
        }
        else
        {
            Application::Get().BlockEvents(true);
        }

        auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
        auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
        auto viewportOffset = ImGui::GetWindowPos();
        m_ViewportBounds[0] = {viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y};
        m_ViewportBounds[1] = {viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y};

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();

        //Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused && !m_ViewportHovered);

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        m_ViewportSize = {viewportPanelSize.x, viewportPanelSize.y};

        auto textureID = Renderer::sceneImage->getImTextureID();
        if (textureID)
        {
            ImGui::Image(textureID, ImVec2(m_ViewportSize.x, m_ViewportSize.y), ImVec2(0, 0), ImVec2(1, 1));
        }
        else
        {
            ImGui::Text("No image available!");
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                AssetHandle handle = *(const AssetHandle*)payload->Data;
                OpenScene(handle);
            }
            ImGui::EndDragDropTarget();
        }

        // Gizmos

        Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();

        if (selectedEntity && m_GizmoType != -1)
        {
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();

            ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
                              m_ViewportBounds[1].x - m_ViewportBounds[0].x,
                              m_ViewportBounds[1].y - m_ViewportBounds[0].y);

            // Camera
            // Runtime camera from entity
            /*auto cameraEntity = m_ActiveScene->GetPrimaryCameraEntity();
            const auto& camera = cameraEntity.GetComponent<CameraComponent>().Camera;
            const glm::mat4& cameraProjection = camera.GetProjection();
            glm::mat4 cameraView = glm::inverse(cameraEntity.GetComponent<TransformComponent>().GetTransform());*/

            // Editor camera
            const glm::mat4& cameraProjection = m_EditorCamera.GetGizmoProjection();

            glm::mat4 cameraView = m_EditorCamera.GetGizmoView();

            // Entity transform
            auto& tc = selectedEntity.GetComponent<TransformComponent>();
            glm::mat4 transform = tc.GetTransform();

            // Snapping
            bool snap = Input::IsKeyPressed(SDL_SCANCODE_LCTRL);
            float snapValue = 0.5f; // Snap to 0.5m for translation/scale
            // Snap to 45 degrees for rotation
            if (m_GizmoType == ImGuizmo::OPERATION::ROTATE)
            {
                snapValue = 45.0f;
            }

            float snapValues[3] = {snapValue, snapValue, snapValue};

            ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
                                 static_cast<ImGuizmo::OPERATION>(m_GizmoType), ImGuizmo::LOCAL,
                                 glm::value_ptr(transform), nullptr, snap ? snapValues : nullptr);

            if (ImGuizmo::IsUsing())
            {
                //translation is position for me maybe change
                glm::vec3 translation, rotation, scale;
                Math::DecomposeTransform(transform, translation, rotation, scale);

                glm::vec3 deltaRotation = rotation - tc.Rotation;
                tc.Translation = translation;
                tc.Rotation += deltaRotation;
                tc.Scale = scale;
            }
        }

        ImGui::End(); // End viewport
        ImGui::PopStyleVar();

        UI_ToolBar();

        ImGui::End(); // End DockSpace
    }

    void EditorLayer::UI_ToolBar()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 2});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2{0, 0});
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0, 0, 0, 0});
        auto& colors = ImGui::GetStyle().Colors; //imguilayer styling ganz unten
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{
                                  colors[ImGuiCol_ButtonHovered].x, colors[ImGuiCol_ButtonHovered].y,
                                  colors[ImGuiCol_ButtonHovered].z, 0.5f
                              });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{
                                  colors[ImGuiCol_ButtonActive].x, colors[ImGuiCol_ButtonActive].y,
                                  colors[ImGuiCol_ButtonActive].z, 0.5f
                              });

        ImGui::Begin("##toolbar", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        float size = ImGui::GetWindowHeight() - 4.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x * 0.5f) - (size * 0.5f));

        bool hasPlayButton = m_SceneState == SceneState::Edit || m_SceneState == SceneState::Play;
        bool hasSimulateButton = m_SceneState == SceneState::Edit || m_SceneState == SceneState::Simulate;
        bool hasPauseButton = m_SceneState != SceneState::Edit;

        if (hasPlayButton)
        {
            {
                Ref<Texture2D> icon = (m_SceneState == SceneState::Edit || m_SceneState == SceneState::Simulate) ? m_IconPlay : m_IconStop;
                if (ImGui::ImageButton("##icon", icon->getImTextureID(), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1),
                                       ImVec4(0, 0, 0, 0)))
                {
                    if (hasSimulateButton)
                    {
                        OnScenePlay();
                    }
                    else if (m_SceneState == SceneState::Play)
                    {
                        OnSceneStop();
                    }
                }
            }
        }
        if (hasSimulateButton)
        {
            if (hasPlayButton)
                ImGui::SameLine();
            {
                Ref<Texture2D> icon = (m_SceneState == SceneState::Edit || m_SceneState == SceneState::Play) ? m_IconSimulate : m_IconStop;
                if (ImGui::ImageButton("##icon2", icon->getImTextureID(), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1),
                                       ImVec4(0, 0, 0, 0)))
                {
                    if (m_SceneState == SceneState::Edit || m_SceneState == SceneState::Play)
                    {
                        OnSceneSimulate();
                    }
                    else if (m_SceneState == SceneState::Simulate)
                    {
                        OnSceneStop();
                    }
                }
            }
        }
        if (hasPauseButton)
        {
            bool isPaused = m_ActiveScene->IsPaused();
            ImGui::SameLine();
            {
                Ref<Texture2D> icon = m_IconPause;
                if (ImGui::ImageButton("##icon3", icon->getImTextureID(), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1),
                                       ImVec4(0, 0, 0, 0)))
                {
                    m_ActiveScene->SetPaused(!isPaused);
                }
            }

            // Step button
            if (isPaused)
            {
                ImGui::SameLine();
                {
                    Ref<Texture2D> icon = m_IconStep;
                    if (ImGui::ImageButton("##icon4", icon->getImTextureID(), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1),
                                           ImVec4(0, 0, 0, 0)))
                    {
                        m_ActiveScene->Step(); // make this tweakableinside imgui instead of hardcoding 1
                    }
                }
            }
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        ImGui::End();
    }

    bool EditorLayer::OnKeyPressed(KeyPressedEvent& e)
    {
        // Shortcuts
        if (e.IsRepeat())
        {
            return false;
        }

        bool control = (Input::IsKeyPressed(SDL_SCANCODE_LCTRL) || Input::IsKeyPressed(SDL_SCANCODE_RCTRL));
        bool shift = (Input::IsKeyPressed(SDL_SCANCODE_LSHIFT) || Input::IsKeyPressed(SDL_SCANCODE_RSHIFT));

        switch (e.GetKeyCode())
        {
        case SDL_SCANCODE_N:
            {
                if (control)
                {
                    NewScene();
                }
                break;
            }
        case SDL_SCANCODE_O:
            {
                if (control)
                {
                    OpenProject();
                }
                break;
            }
        case SDL_SCANCODE_S:
            {
                if (control)
                {
                    if (shift)
                        SaveSceneAs();
                    else
                        SaveScene();
                }
                break;
            }

        // Scene Commands
        case SDL_SCANCODE_D:
            {
                if (control)
                {
                    OnDuplicateEntity();
                }
                break;
            }


        // Gizmos
        case SDL_SCANCODE_Q:
            m_GizmoType = -1;
            break;
        case SDL_SCANCODE_W:
            m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
            break;
        case SDL_SCANCODE_E:
            m_GizmoType = ImGuizmo::OPERATION::ROTATE;
            break;
        case SDL_SCANCODE_R:
            if (control)
            {
                /*ScriptEngine::ReloadAssembly();*/
            }
            else
            {
                m_GizmoType = ImGuizmo::OPERATION::SCALE;
            }
            break;
        case SDL_SCANCODE_DELETE:
            {
                if (Application::Get().GetLayer<ImGuiLayer>()->GetActiveWidgetID() == 0)
                {
                    Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
                    if (selectedEntity)
                    {
                        m_SceneHierarchyPanel.SetSelectedEntity({});
                        m_ActiveScene->DestroyEntity(selectedEntity);
                    }
                }
                break;
            }
        default:
            break;
        }

        return false;
    }

    bool EditorLayer::IsButtonHovered() const
    {
        return true; //maybe useful for imgui ImGui::IsItemHovered()
    }

    bool EditorLayer::OnMouseButtonPressed(VanK::MouseButtonPressedEvent& event)
    {
        if (event.GetMouseButton() == SDL_BUTTON_LEFT)
        {
            if (m_ViewportHovered && !ImGuizmo::IsOver() && !Input::IsKeyPressed(SDL_SCANCODE_LALT))
            {
                m_SceneHierarchyPanel.SetSelectedEntity(m_HoveredEntity);
            }
        }

        return false;
    }

    void EditorLayer::OnOverlayRender()
    {
        if (m_SceneState == SceneState::Play)
        {
            Entity camera = m_ActiveScene->GetPrimaryCameraEntity();
            if (!camera)
                return;

            Renderer::BeginScene(camera.GetComponent<CameraComponent>().Camera, camera.GetComponent<TransformComponent>().GetTransform());
        }
        else
        {
            Renderer::BeginScene(m_EditorCamera);
        }

        if (m_ShowPhysicsColliders)
        {
            // Box Colliders
            {
                auto view = m_ActiveScene->GetAllEntitiesWith<TransformComponent, BoxCollider2DComponent>();
                for (auto entity : view)
                {
                    auto [tc, bc2d] = view.get<TransformComponent, BoxCollider2DComponent>(entity);

                    glm::vec3 translation = tc.Translation + glm::vec3(bc2d.Offset, 0.001f);
                    glm::vec3 scale = tc.Scale * glm::vec3(bc2d.Size * 2.0f, 1.0f);

                    // box2d needs first translation then offset otherwise it offsets the bounding box from center instead of creating from center around
                    glm::mat4 transform = glm::translate(glm::mat4(1.0f), translation)
                        * glm::rotate(glm::mat4(1.0f), tc.Rotation.z, glm::vec3(0.0f, 0.0f, 1.0f))
                        * glm::translate(glm::mat4(1.0f), glm::vec3(bc2d.Offset, 0.001f))
                        * glm::scale(glm::mat4(1.0f), scale * glm::vec3(bc2d.Size * 2.0f, 1.0f));

                    Renderer::DrawRect(transform, glm::vec4(0, 1, 0, 1));
                }
            }
            // Circle Colliders
            {
                auto view = m_ActiveScene->GetAllEntitiesWith<TransformComponent, CircleCollider2DComponent>();
                for (auto entity : view)
                {
                    auto [tc, cc2d] = view.get<TransformComponent, CircleCollider2DComponent>(entity);

                    glm::vec3 translation = tc.Translation + glm::vec3(cc2d.Offset, 0.001f);
                    glm::vec3 scale = tc.Scale * glm::vec3(cc2d.Radius * 2.0f);

                    glm::mat4 transform = glm::translate(glm::mat4(1.0f), translation)
                        * glm::scale(glm::mat4(1.0f), scale);

                    Renderer::DrawCircle(transform, glm::vec4(0, 1, 0, 1), 0.01f);
                }
            }
        }

        // Draw selected entity outline
        if (Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity())
        {
            const TransformComponent& transform = selectedEntity.GetComponent<TransformComponent>();
            Renderer::DrawRect(transform.GetTransform(), glm::vec4(1.0f, 0.5f, 0.0f, 1.0f));
        }
        Renderer::EndScene();
    }

    void EditorLayer::NewProject()
    {
        Project::New();
    }

    bool EditorLayer::OpenProject()
    {
        std::string filepath = Utility::OpenFile("Vank Project *.vproj\0vproj\0");

        if (filepath.empty())
            return false;

        OpenProject(filepath);
        return true;
    }

    void EditorLayer::OpenProject(const std::filesystem::path& path)
    {
        if (Project::Load(path))
        {
            /*ScriptEngine::Init();*/

            AssetHandle startScene = Project::GetActive()->GetConfig().StartScene;
            if (startScene)
                OpenScene(startScene);

            m_ContentBrowserPanel = CreateScope<ContentBrowserPanel>(Project::GetActive());
        }
    }

    void EditorLayer::SaveProject()
    {
        //Project::SaveActive();
    }

    void EditorLayer::NewScene()
    {
        m_EditorScene = CreateRef<Scene>();
        m_ActiveScene = m_EditorScene;

        m_SceneHierarchyPanel.SetContext(m_ActiveScene);

        m_EditorScenePath = std::filesystem::path();
    }

    void EditorLayer::OpenScene()
    {
        /*std::string filepath = Utility::OpenFile("Vank Scene *.vank\0vank\0");
        VK_CORE_ERROR("openscene {0}", filepath);
        if (!filepath.empty())
        {
            OpenScene(filepath);
        }*/
    }

    void EditorLayer::OpenScene(AssetHandle handle)
    {
        VK_CORE_ASSERT(handle);

        if (m_SceneState != SceneState::Edit)
        {
            OnSceneStop();
        }

        Ref<Scene> readOnlyScene = AssetManager::GetAsset<Scene>(handle);
        Ref<Scene> newScene = Scene::Copy(readOnlyScene);

        m_EditorScene = newScene;
        m_SceneHierarchyPanel.SetContext(m_EditorScene);

        m_ActiveScene = m_EditorScene;
        m_EditorScenePath = Project::GetActive()->GetEditorAssetManager()->GetFilePath(handle);
    }

    void EditorLayer::SaveScene()
    {
        if (!m_EditorScenePath.empty())
        {
            SerializeScene(m_ActiveScene, m_EditorScenePath);
        }
        else
        {
            SaveSceneAs();
        }
    }

    void EditorLayer::SaveSceneAs()
    {
        std::string filepath = Utility::SaveFile("Vank Scene *.vank\0vank\0");
        if (!filepath.empty())
        {
            SerializeScene(m_ActiveScene, filepath);
            m_EditorScenePath = filepath;
        }
    }

    void EditorLayer::SerializeScene(Ref<Scene> scene, const std::filesystem::path& path)
    {
        SceneImporter::SaveScene(scene, path);
    }

    void EditorLayer::OnScenePlay()
    {
        if (m_SceneState == SceneState::Simulate)
            OnSceneStop();

        m_SceneState = SceneState::Play;

        m_ActiveScene = Scene::Copy(m_EditorScene);
        m_ActiveScene->OnRuntimeStart();

        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    }

    void EditorLayer::OnSceneSimulate()
    {
        if (m_SceneState == SceneState::Play)
            OnSceneStop();

        m_SceneState = SceneState::Simulate;

        m_ActiveScene = Scene::Copy(m_EditorScene);
        m_ActiveScene->OnSimulationStart();

        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    }

    void EditorLayer::OnSceneStop()
    {
        VK_CORE_ASSERT("OnSceneStop failed no sceneState match", m_SceneState == SceneState::Play || m_SceneState == SceneState::Simulate);

        if (m_SceneState == SceneState::Play)
            m_ActiveScene->OnRuntimeStop();
        else if (m_SceneState == SceneState::Simulate)
            m_ActiveScene->OnSimulationStop();

        m_SceneState = SceneState::Edit;

        m_ActiveScene = m_EditorScene;

        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    }

    void EditorLayer::OnScenePause()
    {
        if (m_SceneState == SceneState::Edit)
            return;

        m_ActiveScene->SetPaused(true);
    }

    void EditorLayer::OnDuplicateEntity()
    {
        if (m_SceneState != SceneState::Edit)
        {
            return;
        }

        Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
        if (selectedEntity)
        {
            Entity newEntity = m_EditorScene->DuplicateEntity(selectedEntity);
            m_SceneHierarchyPanel.SetSelectedEntity(newEntity);
        }
    }
}