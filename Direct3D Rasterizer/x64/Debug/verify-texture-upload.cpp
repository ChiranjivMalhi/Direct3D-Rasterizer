#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <WICTextureLoader.h>
#include <ResourceUploadBatch.h>
#include <cstdio>
#include <future>
using Microsoft::WRL::ComPtr;
static void stage(const char* text) { std::puts(text); std::fflush(stdout); }
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    for (const char* module : {"msvcp140d.dll", "concrt140d.dll", "vcruntime140d.dll", "ucrtbased.dll"}) {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(GetModuleHandleA(module), path, MAX_PATH);
        std::printf("%s: %s\n", module, path);
    }
    stage("Testing standard async/future");
    auto plain = std::async(std::launch::async, [] { return 42; });
    stage("Standard async constructed; calling get");
    if (plain.get() != 42) return 1;
    stage("Standard future passed");
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return 2;
    ComPtr<ID3D12Device> device;
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device.GetAddressOf()));
    std::printf("Device HRESULT=%08lx\n", static_cast<unsigned long>(hr)); std::fflush(stdout);
    if (FAILED(hr)) return 3;
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC desc = {};
    hr = device->CreateCommandQueue(&desc, IID_PPV_ARGS(queue.GetAddressOf()));
    if (FAILED(hr)) return 4;
    stage("Testing empty toolkit upload");
    {
        DirectX::ResourceUploadBatch upload(device.Get());
        upload.Begin();
        auto finished = upload.End(queue.Get());
        stage("Empty upload End returned; calling get");
        finished.get();
    }
    stage("Empty upload passed");
    ComPtr<ID3D12Resource> textures[4];
    {
        DirectX::ResourceUploadBatch upload(device.Get());
        upload.Begin();
        const wchar_t* paths[] = {L"aiStandardSurface2_BaseColor.png", L"aiStandardSurface2_Roughness.png",
            L"aiStandardSurface2_Normal.png", L"aiStandardSurface2_Metallic.png"};
        for (int i = 0; i < 4; ++i) {
            hr = DirectX::CreateWICTextureFromFileEx(device.Get(), upload,
                paths[i], 0, D3D12_RESOURCE_FLAG_NONE,
                static_cast<DirectX::WIC_LOADER_FLAGS>((i == 0 ? DirectX::WIC_LOADER_FORCE_SRGB : DirectX::WIC_LOADER_IGNORE_SRGB) | DirectX::WIC_LOADER_MIP_AUTOGEN),
                textures[i].GetAddressOf());
            std::printf("Texture %d HRESULT=%08lx\n", i, static_cast<unsigned long>(hr)); std::fflush(stdout);
            if (FAILED(hr)) return 5;
        }
        auto finished = upload.End(queue.Get());
        stage("Texture upload End returned; calling get");
        finished.get();
    }
    stage("Texture upload passed");
    return 0;
}
