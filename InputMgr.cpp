// InputManager.cpp
#include "InputMgr.h"
#include <windows.h>

using namespace vkapp;

InputManager::InputManager()
{
    // initialize keyboard state snapshot
    ZeroMemory(m_keyState.data(), m_keyState.size());
    ZeroMemory(m_prevKeyState.data(), m_prevKeyState.size());
    // query OS initial state
    // (optional - we keep arrays zeroed and let Update sample)
}

void InputManager::Update()
{
    // copy previous state
    m_prevKeyState = m_keyState;

    // sample keyboard via GetAsyncKeyState for simplicity
    for (int k = 0; k < MAX_KEYS; ++k) {
        SHORT state = GetAsyncKeyState(k);
        m_keyState[k] = (state & 0x8000) ? 1 : 0;
    }

    // sample mouse
    POINT p;
    if (GetCursorPos(&p)) {
        ScreenToClient(GetActiveWindow(), &p);
        m_mouseX = p.x;
        m_mouseY = p.y;
    }
    // mouse buttons can be sampled similarly
    m_mouseButtons[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    m_mouseButtons[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    m_mouseButtons[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
}

bool InputManager::IsKeyDown(int vkey) const
{
    if (vkey < 0 || vkey >= MAX_KEYS) return false;
    return m_keyState[vkey] != 0;
}

bool InputManager::WasKeyPressed(int vkey) const
{
    if (vkey < 0 || vkey >= MAX_KEYS) return false;
    return (m_keyState[vkey] != 0) && (m_prevKeyState[vkey] == 0);
}

void InputManager::GetMousePosition(int& x, int& y) const
{
    x = m_mouseX; y = m_mouseY;
}

bool InputManager::IsMouseButtonDown(int btn) const
{
    if (btn < 0 || btn >= static_cast<int>(m_mouseButtons.size())) return false;
    return m_mouseButtons[btn];
}

void InputManager::OnRawMouseMove(int x, int y)
{
    m_mouseX = x; m_mouseY = y;
}

void InputManager::OnMouseButton(int btn, bool down)
{
    if (btn < 0 || btn >= static_cast<int>(m_mouseButtons.size())) return;
    m_mouseButtons[btn] = down;
}
