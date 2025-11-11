// App.cpp
#include "App.h"
#include <iostream>
#include <chrono>

// Forward declarations of your engine modules - include real headers in your project
#include "VulkanContext.h"  
#include "Renderer.h"
//#include "../scene/Scene.h"

using namespace vkapp;

App::App(const std::string& title, int width, int height)
    : m_title(title), m_width(width), m_height(height)
{
    m_window = std::make_unique<Win32Window>(m_title.c_str(), m_width, m_height);
    m_inputMgr = std::make_unique<InputManager>();
}

App::~App()
{
    // ensure proper teardown order: renderer -> scene -> vk -> window
    m_renderer.reset();
   // m_scene.reset();
    m_vkContext.reset();
    m_inputMgr.reset();
    m_window.reset();
}

bool App::Init()
{
    if (!m_window->Create()) {
        std::cerr << "Failed to create Win32 window\n";
        return false;
    }

    // Create and initialize VulkanContext
    m_vkContext = std::make_unique<VulkanContext>();
    if (!m_vkContext->Init(m_window->GetHWND(), m_width, m_height)) {
        std::cerr << "Failed to initialize VulkanContext\n";
        return false;
    }

    // Scene + Renderer initialization (replace with your actual constructors)
   // m_scene = std::make_unique<Scene>();
    m_renderer = std::make_unique<Renderer>();

    if (!m_renderer->Init(m_vkContext.get())) {
        std::cerr << "Failed to initialize Renderer\n";
        return false;
    }

    // Hook resize event so app can inform renderer
    m_window->SetOnResizeCallback([this](int w, int h) {
        m_width = w; m_height = h; m_resized = true;
        });

    m_lastTime = std::chrono::steady_clock::now();
    return true;
}

void App::Run()
{
    m_running = true;
    MSG msg = {};

    while (m_running && !m_exitRequested)
    {
        // Process all pending Win32 messages
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))    //CHECK:could be if
        {        
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_QUIT)
            {
                m_running = false;
                break;
            }
        }
        if (!m_running)
            break;

        // Frame timing
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> delta = now - m_lastTime;
        m_lastTime = now;
        double dt = delta.count();

        // If window was resized, notify renderer (handles swapchain recreation)
        if (m_resized)
        {
            m_resized = false;
            m_renderer->OnResize(m_width, m_height);
        }

        // Update input state
        m_window->PollEvents();
        m_inputMgr->Update();

        // Per-frame update & render
        Tick(dt);
        RenderFrame(dt);
    }

    // flush GPU & cleanup
    m_vkContext->WaitIdle();
}

void App::Tick(double dt)
{
    // Update game/app logic & scene
   // m_scene->Update(dt, *m_input);
    // you can add fixed-step physics or other subsystems here
}

void App::RenderFrame(double dt)
{
    // Call renderer to draw the scene
    //if (!m_renderer->BeginFrame()) {
    //    // If BeginFrame fails (e.g. swapchain out of date), recreate swapchain inside renderer
    //    m_renderer->OnResize(m_width, m_height);
    //    return;
    //}

    //m_renderer->Render(*m_scene, dt);
    //m_renderer->EndFrame();
}

void App::RequestExit()
{
    m_exitRequested = true;
}
