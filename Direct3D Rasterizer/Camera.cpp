#include "Camera.h"

#include <Keyboard.h>
#include <Mouse.h>
#include <algorithm>
#include <cmath>

using namespace DirectX;

struct Camera::InputDevices {
    Mouse mouse;
    Keyboard keyboard;
    Mouse::ButtonStateTracker mouseButtons;
    Keyboard::KeyboardStateTracker keys;
    bool mouseLook = false;
};

Camera::Camera() : m_input(std::make_unique<InputDevices>()) {}

Camera::~Camera()
{
    StopMouseLook();
}

void Camera::AttachWindow(HWND window)
{
    m_input->mouse.SetWindow(window);
    m_window = window;
}

void Camera::StopMouseLook()
{
    m_input->mouseLook = false;
    if (m_window && IsWindow(m_window)) {
        m_input->mouse.SetMode(Mouse::MODE_ABSOLUTE);
    }
}

void Camera::ProcessMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!m_window) return;

    switch (message) {
    case WM_ACTIVATE:
    case WM_ACTIVATEAPP:
        Keyboard::ProcessMessage(message, wParam, lParam);
        Mouse::ProcessMessage(message, wParam, lParam);
        if ((message == WM_ACTIVATEAPP && !wParam) ||
            (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE)) {
            StopMouseLook();
            m_input->keys.Reset();
            m_input->mouseButtons.Reset();
        }
        break;

    case WM_INPUT:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_MOUSEHOVER:
        Mouse::ProcessMessage(message, wParam, lParam);
        break;

    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        Keyboard::ProcessMessage(message, wParam, lParam);
        break;

    case WM_KILLFOCUS:
        StopMouseLook();
        m_input->keyboard.Reset();
        m_input->keys.Reset();
        m_input->mouseButtons.Reset();
        break;

    case WM_DESTROY:
        StopMouseLook();
        break;

    case WM_NCDESTROY:
        m_window = nullptr;
        break;
    }
}

void Camera::Update(float deltaSeconds)
{
    // Read relative mouse deltas exactly once per frame.
    const auto mouse = m_input->mouse.GetState();
    const auto keys = m_input->keyboard.GetState();
    m_input->mouseButtons.Update(mouse);
    m_input->keys.Update(keys);

    if (!m_window || GetForegroundWindow() != m_window) {
        StopMouseLook();
        m_input->mouse.EndOfInputFrame();
        return;
    }

    if (m_input->mouseButtons.rightButton == Mouse::ButtonStateTracker::PRESSED) {
        m_input->mouseLook = true;
    }
    if (keys.Escape ||
        m_input->mouseButtons.rightButton == Mouse::ButtonStateTracker::RELEASED) {
        m_input->mouseLook = false;
    }

    m_input->mouse.SetMode(m_input->mouseLook
        ? Mouse::MODE_RELATIVE : Mouse::MODE_ABSOLUTE);

    CameraInput input;
    input.forward = static_cast<float>(keys.W) - static_cast<float>(keys.S);
    input.right = static_cast<float>(keys.D) - static_cast<float>(keys.A);
    input.vertical = static_cast<float>(keys.E) - static_cast<float>(keys.Q);
    input.boost = keys.LeftShift || keys.RightShift;
    input.reset = m_input->keys.pressed.R;

    if (m_input->mouseLook && mouse.positionMode == Mouse::MODE_RELATIVE) {
        input.mouseX = static_cast<float>(mouse.x);
        input.mouseY = static_cast<float>(mouse.y);
    }

    ApplyInput(input, deltaSeconds);
    m_input->mouse.EndOfInputFrame();
}

void Camera::ApplyInput(const CameraInput& input, float deltaSeconds)
{
    if (input.reset) {
        Reset();
        return;
    }

    // Mouse input is a displacement, so it is not multiplied by elapsed time.
    m_yaw = std::remainder(m_yaw + input.mouseX * mouseSensitivity, XM_2PI);
    m_pitch = std::clamp(m_pitch - input.mouseY * mouseSensitivity,
        -XM_PIDIV2 + 0.01f, XM_PIDIV2 - 0.01f);

    const CameraData basis = GetShaderData();
    XMVECTOR movement = XMVectorAdd(
        XMVectorScale(XMLoadFloat4(&basis.forward), input.forward),
        XMVectorScale(XMLoadFloat4(&basis.right), input.right));
    movement = XMVectorAdd(movement, XMVectorSet(0, input.vertical, 0, 0));

    if (XMVectorGetX(XMVector3LengthSq(movement)) > 0.0f) {
        const float elapsed = std::clamp(deltaSeconds, 0.0f, 0.1f);
        const float speed = moveSpeed * (input.boost ? boostMultiplier : 1.0f);
        const XMVECTOR position = XMVectorAdd(XMLoadFloat3(&m_position),
            XMVectorScale(XMVector3Normalize(movement), speed * elapsed));
        XMStoreFloat3(&m_position, position);
    }
}

void Camera::Reset()
{
    m_position = {0.0f, 8.0f, -20.0f};
    m_yaw = 0.0f;
    m_pitch = -0.38050638f;
}

CameraData Camera::GetShaderData() const
{
    const XMVECTOR forward = XMVectorSet(
        std::sin(m_yaw) * std::cos(m_pitch),
        std::sin(m_pitch),
        std::cos(m_yaw) * std::cos(m_pitch), 0.0f);
    const XMVECTOR right = XMVector3Normalize(
        XMVector3Cross(XMVectorSet(0, 1, 0, 0), forward));
    const XMVECTOR up = XMVector3Cross(forward, right);

    CameraData data;
    XMStoreFloat4(&data.position,
        XMVectorSet(m_position.x, m_position.y, m_position.z, 1.0f));
    XMStoreFloat4(&data.forward, forward);
    XMStoreFloat4(&data.right, right);
    XMStoreFloat4(&data.up, up);
    return data;
}
