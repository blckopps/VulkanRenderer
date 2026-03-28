#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ================================================================
//  Camera
//  Stores position, orientation (yaw/pitch), and projection params.
//  CameraController drives it each frame via Update().
// ================================================================
class Camera
{
public:
    // World-space position
    glm::vec3 position{ 0.0f, 0.0f, 3.0f };

    // Euler angles (degrees)
    float yaw = -90.0f;   // -90 so forward starts pointing -Z
    float pitch = 0.0f;

    // Projection
    float fov = 45.0f;
    float aspect = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    // ---- derived vectors (call Recalculate() after changing yaw/pitch) ----
    glm::vec3 forward{ 0.0f,  0.0f, -1.0f };
    glm::vec3 right{ 1.0f,  0.0f,  0.0f };
    glm::vec3 up{ 0.0f,  1.0f,  0.0f };

    // World up — never changes
    static constexpr glm::vec3 WORLD_UP{ 0.0f, 1.0f, 0.0f };

    // ---- orbit state (used by orbit mode) ----
    glm::vec3 orbitTarget{ 0.0f, 0.0f, 0.0f };
    float     orbitRadius = 3.0f;

    // ------------------------------------------------------------------
    void SetAspect(float width, float height)
    {
        if (height > 0.0f)
            aspect = width / height;
    }

    // Rebuild forward/right/up from yaw and pitch.
    // Call this whenever yaw or pitch changes.
    void Recalculate()
    {
        float yawR = glm::radians(yaw);
        float pitchR = glm::radians(pitch);

        forward.x = cosf(pitchR) * cosf(yawR);
        forward.y = sinf(pitchR);
        forward.z = cosf(pitchR) * sinf(yawR);
        forward = glm::normalize(forward);

        right = glm::normalize(glm::cross(forward, WORLD_UP));
        up = glm::normalize(glm::cross(right, forward));
    }

    glm::mat4 GetView() const
    {
        return glm::lookAt(position, position + forward, up);
    }

    glm::mat4 GetProjection() const
    {
        glm::mat4 proj = glm::perspective(
            glm::radians(fov), aspect, nearPlane, farPlane);
        proj[1][1] *= -1.0f;  // flip Y for Vulkan NDC
        return proj;
    }
};