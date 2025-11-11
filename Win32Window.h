// Win32Window.h
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <functional>

namespace vkapp {

    class Win32Window {
    public:
        using ResizeCallback = std::function<void(int width, int height)>;

        Win32Window(const char* title, int width = 1280, int height = 720);
        ~Win32Window();

        // Create and show the window 
        bool Create();

        // Poll OS events (wrapper - currently a thin call)
        void PollEvents();

        // Window handle accessors
        HWND GetHWND() const noexcept { return m_hwnd; }
        int Width() const noexcept { return m_width; }
        int Height() const noexcept { return m_height; }

        void SetOnResizeCallback(ResizeCallback cb) { m_onResize = std::move(cb); }

    private:
        //To let windows handle the events. There is no 'this' here.
        static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

        //This will handle every 'Win32Window' object's event.
        LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

        std::string m_title;
        int m_width;
        int m_height;
        HWND m_hwnd = nullptr;
        HINSTANCE m_hInstance = nullptr;
        ResizeCallback m_onResize;
        ATOM m_windowClassAtom = 0;
    };
} // namespace vkapp
