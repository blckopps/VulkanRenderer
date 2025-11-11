// InputManager.h
#pragma once
#include <array>
#include <functional>

namespace vkapp {

    // A tiny input state manager for keyboard & mouse
    class InputManager {
    public:
        InputManager();

        // Call once per frame to capture / update transient state
        void Update();

        // Keyboard queries
        bool IsKeyDown(int vkey) const;     // vkey = VK_* (Win32 virtual key codes)
        bool WasKeyPressed(int vkey) const; // one-frame pressed

        // Mouse queries
        void GetMousePosition(int& x, int& y) const;
        bool IsMouseButtonDown(int btn) const; // 0=left,1=right,2=middle

        // Convenience: set from Win32 events (optional)
        void OnRawMouseMove(int x, int y);
        void OnMouseButton(int btn, bool down);

    private:
        static constexpr int MAX_KEYS = 256;
        std::array<unsigned char, MAX_KEYS> m_keyState = {};
        std::array<unsigned char, MAX_KEYS> m_prevKeyState = {};

        int m_mouseX = 0;
        int m_mouseY = 0;
        std::array<bool, 3> m_mouseButtons = {};
    };
} // namespace vkapp
