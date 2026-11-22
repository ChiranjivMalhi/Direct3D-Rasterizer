#pragma once

#include <windows.h>
#include <DirectXMath.h>
#include <memory>

// Four 16-byte groups, matching the camera fields in shader.hlsl.
struct CameraData {
    DirectX::XMFLOAT4 position;
    DirectX::XMFLOAT4 forward;
    DirectX::XMFLOAT4 right;
    DirectX::XMFLOAT4 up;
};

static_assert(sizeof(CameraData) == 64);

struct CameraInput {
    float forward = 0.0f;
    float right = 0.0f;
    float vertical = 0.0f;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    bool boost = false;
    bool reset = false;
};

class Camera {
public:
    Camera();
    ~Camera();

    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;

    void AttachWindow(HWND window);
    void ProcessMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void Update(float deltaSeconds);

    // The same movement calculation can also accept scripted/test input.
    void ApplyInput(const CameraInput& input, float deltaSeconds);
    void Reset();
    CameraData GetShaderData() const;

    float moveSpeed = 10.0f;
    float mouseSensitivity = 0.002f;
    float boostMultiplier = 3.0f;

private:
    void StopMouseLook();

    struct InputDevices;
    std::unique_ptr<InputDevices> m_input;
    HWND m_window = nullptr;
    DirectX::XMFLOAT3 m_position = {0.0f, 8.0f, -20.0f};
    float m_yaw = 0.0f;
    float m_pitch = -0.38050638f;
};
