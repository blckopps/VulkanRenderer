#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera
{
public:
    glm::vec3 position{ 0.0f, 0.0f, 2.0f };
    glm::vec3 target{ 0.0f, 0.0f, 0.0f };
    glm::vec3 up{ 0.0f, 1.0f, 0.0f };
    float fov = 45.0f;
    float aspect = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    glm::mat4 GetView() const {
        return glm::lookAt(position, target, up);
    }
    glm::mat4 GetProjection() const {
        glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
        proj[1][1] *= -1; // flip Y for Vulkan
        return proj;
    }
};
