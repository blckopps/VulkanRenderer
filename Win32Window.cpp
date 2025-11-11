// Win32Window.cpp
#include "Win32Window.h"
#include <stdexcept>
#include <cassert>

using namespace vkapp;

static constexpr const char* WINDOW_CLASS_NAME = "Win32VKRenderer";

Win32Window::Win32Window(const char* title, int width, int height)
    : m_title(title), m_width(width), m_height(height)
{
    m_hInstance = GetModuleHandle(nullptr);
}

Win32Window::~Win32Window()
{
    if (m_hwnd) DestroyWindow(m_hwnd);
    if (m_windowClassAtom) UnregisterClass(MAKEINTATOM(m_windowClassAtom), m_hInstance);
}

bool Win32Window::Create()
{
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Win32Window::StaticWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"Win32VKRenderer";

    m_windowClassAtom = RegisterClassEx(&wc);
    if (!m_windowClassAtom) {
        MessageBox(NULL, L"Failed to register class!!!", L"Error", MB_OK);
        return false;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT wr = { 0, 0, m_width, m_height };
    AdjustWindowRect(&wr, style, FALSE);

    m_hwnd = CreateWindowEx(
        0,
        L"Win32VKRenderer",
        L"VKRenderer",                      //FIX:(LPCWSTR)m_title.c_str(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left,
        wr.bottom - wr.top,
        nullptr,
        nullptr,
        m_hInstance,
        this // pass 'this' as lpParam for association in WM_CREATE
    );

    if (!m_hwnd)
    {
        MessageBox(NULL, L"Failed to create window !!!", L"Error", MB_OK);
        return false;
    }
      

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    return true;
}

void Win32Window::PollEvents()
{
    // Currently the app's message pump handles messages (App::Run).
    // This method is kept in case you want to process raw input or peek specific messages here later.
}

LRESULT CALLBACK Win32Window::StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
    {
        // store the 'this' pointer we passed during CreateWindowEx. 
        // This will be use to call our 'Win32Window' object's events
        const CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }

    Win32Window* wnd = reinterpret_cast<Win32Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    if (wnd)
    {
        //Handle our 'Win32Window' object's events.
        return wnd->WndProc(hWnd, msg, wParam, lParam);
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

//Handle 'Win32Window' class object's events
//Every window created with this clas will get this callback.
LRESULT Win32Window::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        m_width = width;
        m_height = height;
        if (m_onResize) m_onResize(width, height);
        return 0;
    }
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}
