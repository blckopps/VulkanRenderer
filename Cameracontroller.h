#pragma once
#include "Camera.h"
#include "InputMgr.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ================================================================
//  CameraController
//
//  Two modes selectable at runtime:
//
//  ORBIT  — rotates around orbitTarget.
//           Left-drag  : orbit (yaw / pitch)
//           Right-drag : pan target
//           Scroll     : zoom (move along forward axis)
//
//  FLY    — free-fly first-person.
//           WASD       : move forward/back/left/right
//           Q / E      : move down / up
//           Right-drag : look (yaw / pitch)
//           Scroll     : change move speed
//
//  Toggle between modes with the Tab key.
// ================================================================
class CameraController
{
public:
    enum class Mode { Orbit, Fly };

    // Tuning knobs — feel free to tweak
    float orbitSensitivity = 0.3f;   // degrees per pixel dragged
    float panSensitivity = 0.005f; // world units per pixel
    float zoomSensitivity = 0.3f;   // world units per scroll tick
    float flySensitivity = 0.2f;   // degrees per pixel dragged
    float flySpeed = 2.0f;   // world units per second (base)
    float flySpeedMin = 0.5f;
    float flySpeedMax = 20.0f;

    Mode mode = Mode::Fly;

    // ------------------------------------------------------------------
    void Init(Camera* camera)
    {
        m_camera = camera;
        m_camera->Recalculate();

        // Initialise orbit from the camera's starting position
        m_camera->orbitTarget = glm::vec3(0.0f);
        m_camera->orbitRadius =
            glm::length(m_camera->position - m_camera->orbitTarget);

        // Derive starting yaw/pitch from the camera's forward vector
        // so the first frame doesn't snap
        _SyncAnglesFromPosition();
    }

    // Call once per frame from App::Tick(dt)
    void Update(vkapp::InputManager& input, float dt)
    {
        // Tab: toggle mode
        if (input.WasKeyPressed(VK_TAB))
        {
            mode = (mode == Mode::Orbit) ? Mode::Fly : Mode::Orbit;
            if (mode == Mode::Orbit)
                _SyncOrbitFromCamera();   // re-derive orbit params
        }

        if (mode == Mode::Orbit)
            _UpdateOrbit(input, dt);
        else
            _UpdateFly(input, dt);

        m_camera->Recalculate();
    }

private:
    Camera* m_camera = nullptr;

    // Mouse state from previous frame
    int  m_prevMouseX = 0;
    int  m_prevMouseY = 0;
    bool m_firstFrame = true;

    // ---- Orbit update -----------------------------------------------
    void _UpdateOrbit(vkapp::InputManager& input, float /*dt*/)
    {
        int mx, my;
        input.GetMousePosition(mx, my);

        const int dx = m_firstFrame ? 0 : mx - m_prevMouseX;
        const int dy = m_firstFrame ? 0 : my - m_prevMouseY;
        m_firstFrame = false;

        // Left mouse button: orbit
        if (input.IsMouseButtonDown(0))
        {
            m_camera->yaw += dx * orbitSensitivity;
            m_camera->pitch -= dy * orbitSensitivity;
            m_camera->pitch = glm::clamp(m_camera->pitch, -89.0f, 89.0f);

            _RebuildOrbitPosition();
        }

        // Right mouse button: pan target
        if (input.IsMouseButtonDown(1))
        {
            m_camera->Recalculate();  // make sure right/up are fresh

            glm::vec3 panDelta =
                -m_camera->right * (float)dx * panSensitivity
                + m_camera->up * (float)dy * panSensitivity;

            m_camera->orbitTarget += panDelta;
            _RebuildOrbitPosition();
        }

        m_prevMouseX = mx;
        m_prevMouseY = my;
    }

    // Reconstruct camera position from orbit angles + target + radius
    void _RebuildOrbitPosition()
    {
        float yawR = glm::radians(m_camera->yaw);
        float pitchR = glm::radians(m_camera->pitch);
        float r = m_camera->orbitRadius;

        glm::vec3 offset;
        offset.x = r * cosf(pitchR) * cosf(yawR);
        offset.y = r * sinf(pitchR);
        offset.z = r * cosf(pitchR) * sinf(yawR);

        m_camera->position = m_camera->orbitTarget + offset;

        // Point forward toward the target
        m_camera->forward =
            glm::normalize(m_camera->orbitTarget - m_camera->position);
    }

    // ---- Fly update -------------------------------------------------
    void _UpdateFly(vkapp::InputManager& input, float dt)
    {
        int mx, my;
        input.GetMousePosition(mx, my);

        const int dx = m_firstFrame ? 0 : mx - m_prevMouseX;
        const int dy = m_firstFrame ? 0 : my - m_prevMouseY;
        m_firstFrame = false;

        // Right mouse button held: look around
        if (input.IsMouseButtonDown(1))
        {
            m_camera->yaw += dx * flySensitivity;
            m_camera->pitch -= dy * flySensitivity;
            m_camera->pitch = glm::clamp(m_camera->pitch, -89.0f, 89.0f);
        }

        m_camera->Recalculate();

        // WASD + Q/E movement
        glm::vec3 move(0.0f);

        if (input.IsKeyDown('W')) move += m_camera->forward;
        if (input.IsKeyDown('S')) move -= m_camera->forward;
        if (input.IsKeyDown('D')) move += m_camera->right;
        if (input.IsKeyDown('A')) move -= m_camera->right;
        if (input.IsKeyDown('E')) move += glm::vec3(0, 1, 0);
        if (input.IsKeyDown('Q')) move -= glm::vec3(0, 1, 0);

        // Shift: speed boost
        float speed = flySpeed;
        if (input.IsKeyDown(VK_SHIFT)) speed *= 3.0f;

        if (glm::length(move) > 0.0f)
            m_camera->position += glm::normalize(move) * speed * dt;

        m_prevMouseX = mx;
        m_prevMouseY = my;
    }

    // ---- Helpers ----------------------------------------------------

    // Derive yaw/pitch from current forward vector (used on mode switch)
    void _SyncAnglesFromPosition()
    {
        glm::vec3 fwd = glm::normalize(
            m_camera->orbitTarget - m_camera->position);

        m_camera->pitch = glm::degrees(asinf(fwd.y));
        m_camera->yaw = glm::degrees(atan2f(fwd.z, fwd.x));
    }

    // Re-derive orbit radius/target when switching back to orbit mode
    void _SyncOrbitFromCamera()
    {
        // Project camera position forward to find a sensible target
        m_camera->orbitTarget =
            m_camera->position + m_camera->forward * m_camera->orbitRadius;
    }
};