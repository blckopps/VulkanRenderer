// App.h
#pragma once

//C++
#include <memory>
#include <string>
#include <chrono>

#pragma comment(lib,"vulkan-1.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"kernel32.lib")
#pragma comment(lib,"gdi32.lib")

//Local Header files
#include "Win32Window.h"
#include "InputMgr.h"
#include "Camera.h"
#include "CameraController.h"

namespace vkapp {

    class VulkanContext;
    class Renderer;
    class Scene;

    class App {
    public:
        App(const std::string& title, int width = 1280, int height = 720);
        ~App();

        // initialize subsystems (VulkanContext, ResourceManager, Renderer, Scene, etc.)
        bool Init();

        // Main loop - blocks until window is closed
        void Run();

        // Signal to request the app to stop (safe to call from other threads)
        void RequestExit();

        // Accessors
        Win32Window& Window() noexcept { return *m_window; }
        InputManager& Input() noexcept { return *m_inputMgr; }

    private:
        void Tick(double dt);        // per-frame update (delta seconds)
        void RenderFrame(double dt); // per-frame render

        std::unique_ptr<Win32Window> m_window;
        std::unique_ptr<InputManager> m_inputMgr;

        // Engine subsystems (owned here or injected in Init)
        std::unique_ptr<VulkanContext> m_vkContext;
        std::unique_ptr<Renderer>      m_renderer;
        //std::unique_ptr<Scene>        m_scene;

        // Camera — owned by App, shared with Renderer via SetCamera()
        Camera           m_camera;
        CameraController m_cameraController;

        std::string m_title;
        int m_width;
        int m_height;

        bool m_running = false;
        bool m_exitRequested = false;
        bool m_resized = false;

        // Frame timing
        std::chrono::steady_clock::time_point m_lastTime;
    };
} // namespace vkapp