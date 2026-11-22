#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <DirectXMath.h>
#include <map>
#include<tuple>
#include <chrono>
#include "Camera.h"

#include <wrl/client.h>
#include <objbase.h>
#include <WICTextureLoader.h>
#include <ResourceUploadBatch.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
	DirectX::XMFLOAT2 uv;
};

struct Model {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indicies;
    bool hasUVs = true;
};

struct Material {
	DirectX::XMFLOAT3 BaseColor = { 0.1f, 0.1f, 0.1f };
    float roughness = 0.5f;
    float metallic = 0.0f;
};

Material modelMaterial;
Material planeMaterial;


struct DrawConstants {
    unsigned int angle = 0;
    float roughness = 0.5f;
    float metallic = 0.0f;
    unsigned int textureFlags = 0;

    DirectX::XMFLOAT3 baseColor = { 0.8f, 0.8f, 0.8f };
    float paddingColor = 0.0f;

    DirectX::XMFLOAT3 objectScale = { 1.0f, 1.0f, 1.0f };
    float paddingScale = 0.0f;

    DirectX::XMFLOAT3 objectPosition = { 0.0f, 0.0f, 0.0f };
    float paddingPosition = 0.0f;

    DirectX::XMFLOAT4X4 lightViewProjection{};

    DirectX::XMFLOAT3 lightDirectionWorld = { 0.0f, 1.0f, 0.0f };
    float paddingLight = 0.0f;

    CameraData camera{};
};

static_assert(sizeof(DrawConstants) == 208);
constexpr UINT drawConstantCount = static_cast<UINT>(sizeof(DrawConstants) / sizeof(unsigned int));
static_assert(drawConstantCount + 2 <= 64, "Root constants and tables must fit the D3D12 root signature budget.");

Model load_model_from_obj(const std::string& path) {
    std::ifstream file(path);  // Open the file

    if (!file.is_open()) {
        return {};
    }

    Model model;
    std::vector<DirectX::XMFLOAT3> temp_positions;
    std::vector<DirectX::XMFLOAT3> temp_normals;
    std::map<
        std::tuple<unsigned int, unsigned int, unsigned int>,
        unsigned int
    > index_map;
    std::vector<unsigned int> temp_face_indices;
    std::vector<DirectX::XMFLOAT2> temp_uvs;

    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);

        std::string prefix;
        ss >> prefix;

        if (prefix == "v") { //vertex position
            DirectX::XMFLOAT3 position;
            ss >> position.x >> position.y >> position.z;
            temp_positions.push_back(position);
        }
        else if (prefix == "vn") { //vertex normal
            DirectX::XMFLOAT3 normal;
            ss >> normal.x >> normal.y >> normal.z;
            temp_normals.push_back(normal);
        }
        else if (prefix == "vt") { //vertex texture coordinate
            DirectX::XMFLOAT2 uv{};
            ss >> uv.x >> uv.y;

            //uv.y = 1.0f - uv.y;
            temp_uvs.push_back(uv);
        }
        else if (prefix == "f") {
            temp_face_indices.clear();

            std::string vertexindex_slashslash_normalindex;
            while (ss >> vertexindex_slashslash_normalindex) {
                // OBJ faces can use v/vt/vn or v//vn. Texture indices are unused.
                const size_t first_slash = vertexindex_slashslash_normalindex.find('/');
                const size_t last_slash = vertexindex_slashslash_normalindex.rfind('/');
                if (first_slash == std::string::npos || first_slash == last_slash ||
                    last_slash + 1 == vertexindex_slashslash_normalindex.size()) {
                    return {}; // This renderer requires a normal for each vertex.
                }

                unsigned int vertex_index;
                unsigned int normal_index;
                unsigned int uv_index = UINT_MAX;
                try {
                    const auto resolve_index = [](const std::string& text, size_t count) {
                        size_t parsed = 0;
                        const int obj_index = std::stoi(text, &parsed);
                        const long long index = obj_index > 0 ? obj_index - 1LL :
                            static_cast<long long>(count) + obj_index;
                        if (parsed != text.size() || obj_index == 0 || index < 0 ||
                            index >= static_cast<long long>(count)) {
                            throw std::out_of_range("Invalid OBJ index");
                        }
                        return static_cast<unsigned int>(index);
                    };
                    vertex_index = resolve_index(vertexindex_slashslash_normalindex.substr(0, first_slash), temp_positions.size());
                    normal_index = resolve_index(vertexindex_slashslash_normalindex.substr(last_slash + 1), temp_normals.size());
                    if (last_slash > first_slash + 1) {
                        uv_index = resolve_index(
                            vertexindex_slashslash_normalindex.substr(
                                first_slash + 1,
                                last_slash - first_slash - 1
                            ),
                            temp_uvs.size()
                        );
                    }
                    else {
                        model.hasUVs = false;
                    }
                }
                catch (const std::exception&) {
                    return {};
                }

                // Check if the vertex and normal index pair already exists in the map
                const auto key = std::make_tuple(
                    vertex_index,
                    uv_index,
                    normal_index
                );

                auto it = index_map.find(key);

                if (it != index_map.end()) {
                    temp_face_indices.push_back(it->second);
                }
                else {
                    // If it doesn't exist, create a new vertex and add it to the model
                    Vertex vertex;
                    vertex.position = temp_positions[vertex_index];
                    vertex.normal = temp_normals[normal_index];
                    vertex.uv = uv_index != UINT_MAX
                        ? temp_uvs[uv_index]
                        : DirectX::XMFLOAT2{0.0f, 0.0f};
                    unsigned int new_index = static_cast<unsigned int>(model.vertices.size());
                    model.vertices.push_back(vertex);
                    // Store the new index in the map
                    index_map[key] = new_index;
                    temp_face_indices.push_back(new_index);
                }
            }

            for (int i = 0; i < (int)temp_face_indices.size() - 2; i++) {
                model.indicies.push_back(temp_face_indices[0]);
                model.indicies.push_back(temp_face_indices[i + 1]);
                model.indicies.push_back(temp_face_indices[i + 2]);
            }
        }
    }
    file.close();  // Close the file
    return model;
}

void set_blend_state(D3D12_BLEND_DESC& blend_desc) {
    blend_desc = {};

    blend_desc.AlphaToCoverageEnable = FALSE;
    blend_desc.IndependentBlendEnable = FALSE;

    D3D12_RENDER_TARGET_BLEND_DESC default_render_target_blend_desc = {};
    default_render_target_blend_desc.BlendEnable = FALSE;
    default_render_target_blend_desc.LogicOpEnable = FALSE;
    default_render_target_blend_desc.SrcBlend = D3D12_BLEND_ONE;
    default_render_target_blend_desc.DestBlend = D3D12_BLEND_ZERO;
    default_render_target_blend_desc.BlendOp = D3D12_BLEND_OP_ADD;
    default_render_target_blend_desc.SrcBlendAlpha = D3D12_BLEND_ONE;
    default_render_target_blend_desc.DestBlendAlpha = D3D12_BLEND_ZERO;
    default_render_target_blend_desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    default_render_target_blend_desc.LogicOp = D3D12_LOGIC_OP_NOOP;
    default_render_target_blend_desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
        blend_desc.RenderTarget[i] = default_render_target_blend_desc;
    }
}

void set_rasterizer_state(D3D12_RASTERIZER_DESC& rasterizer_desc) {
    rasterizer_desc = {};

    rasterizer_desc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizer_desc.CullMode = D3D12_CULL_MODE_BACK;
    rasterizer_desc.FrontCounterClockwise = FALSE;
    rasterizer_desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizer_desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizer_desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizer_desc.DepthClipEnable = TRUE;
    rasterizer_desc.MultisampleEnable = FALSE;
    rasterizer_desc.AntialiasedLineEnable = FALSE;
    rasterizer_desc.ForcedSampleCount = 0;
    rasterizer_desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
}

void set_depth_stencil_state(D3D12_DEPTH_STENCIL_DESC& depth_stencil_desc) {
    depth_stencil_desc = {};

    depth_stencil_desc.DepthEnable = TRUE;
    depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    depth_stencil_desc.StencilEnable = FALSE;
    depth_stencil_desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depth_stencil_desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

    depth_stencil_desc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    depth_stencil_desc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    depth_stencil_desc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    depth_stencil_desc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    depth_stencil_desc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    depth_stencil_desc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    depth_stencil_desc.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    depth_stencil_desc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Camera* camera = reinterpret_cast<Camera*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        camera = static_cast<Camera*>(create->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(camera));
    }
    if (camera) {
        camera->ProcessMessage(message, wParam, lParam);
    }
    switch (message) {
    case WM_DESTROY:
    {
        PostQuitMessage(0);
        return 0;
    }
    break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}





int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    
    const HRESULT comResult = CoInitializeEx(
        nullptr,
        COINIT_MULTITHREADED
    );

    if (FAILED(comResult)) return 1;
    
    const int width = 800;
    const int height = 800;
    Camera camera;

    // Register a simple window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DirectX12Triangle";
    RegisterClass(&wc);

    // Create a window
    HWND hwnd = CreateWindow(wc.lpszClassName, L"DirectX 12 Triangle", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr, hInstance, &camera);
    if (!hwnd) return 1;
    camera.AttachWindow(hwnd);
    ShowWindow(hwnd, nCmdShow);


    ID3D12Device* device = nullptr;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));

    //command queue decides which order the command lists should execute. In our case, we only have one command list.
    ID3D12CommandQueue* command_queue = nullptr;
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue));

    //command allocator is used to allocate memory on the GPU for commands
    ID3D12CommandAllocator* command_allocator = nullptr;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator));
    hr = command_allocator->Reset();

    //command list is used to store a list of commands that we want to execute on the GPU
    ID3D12GraphicsCommandList* command_list = nullptr;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator, nullptr, IID_PPV_ARGS(&command_list));
    hr = command_list->Close();
    

    //create swap chain
    IDXGIFactory4* factory = nullptr;
    hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return 1;
    }

    DXGI_SWAP_CHAIN_DESC swap_chain_desc = {};
    swap_chain_desc.BufferCount = 2;
    swap_chain_desc.BufferDesc.Width = width;
    swap_chain_desc.BufferDesc.Height = height;
    swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_chain_desc.OutputWindow = hwnd;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.Windowed = TRUE;

    IDXGISwapChain* temp_swap_chain = nullptr;
    hr = factory->CreateSwapChain(command_queue, &swap_chain_desc, &temp_swap_chain);
    factory->Release();

    //cast the swap chain to IDXGISwapChain3 to leverage the latest features
    IDXGISwapChain3* swap_chain = {};
    hr = temp_swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain));
    temp_swap_chain->Release();
    temp_swap_chain = nullptr;

    //memory descriptor heap to store render target views(RTV). Descriptor describes how to interperate resource memory.
    ID3D12DescriptorHeap* rtv_heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
    rtv_heap_desc.NumDescriptors = 2;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr = device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap));

    ID3D12Resource* render_targets[2];

    UINT rtv_increment_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->GetCPUDescriptorHandleForHeapStart());
        for (UINT i = 0; i < 2; i++) {
            hr = swap_chain->GetBuffer(i, IID_PPV_ARGS(&render_targets[i]));

            device->CreateRenderTargetView(render_targets[i], nullptr, rtv_handle);
            rtv_handle.ptr += rtv_increment_size;
        }
    }


    //memory descriptor heap to store render target views(DSV). Descriptor describes how to interperate resource memory.
    ID3D12DescriptorHeap* dsv_heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
    dsv_heap_desc.NumDescriptors = 1;
    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hr = device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&dsv_heap));

    ID3D12Resource* depth_stencil_buffer = nullptr;

    UINT dsv_increment_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    {
        D3D12_HEAP_PROPERTIES heap_properties = {};
        heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap_properties.CreationNodeMask = 1;
        heap_properties.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC resource_desc = {};
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resource_desc.Alignment = 0;
        resource_desc.Width = width;
        resource_desc.Height = height;
        resource_desc.DepthOrArraySize = 1;
        resource_desc.MipLevels = 1;
        resource_desc.Format = DXGI_FORMAT_D32_FLOAT;
        resource_desc.SampleDesc.Count = 1;
        resource_desc.SampleDesc.Quality = 0;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clear_value = {};
        clear_value.Format = DXGI_FORMAT_D32_FLOAT;
        clear_value.DepthStencil.Depth = 1.0f;
        clear_value.DepthStencil.Stencil = 0;

        hr = device->CreateCommittedResource(
            &heap_properties,
            D3D12_HEAP_FLAG_NONE,
            &resource_desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clear_value,
            IID_PPV_ARGS(&depth_stencil_buffer));

        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle(dsv_heap->GetCPUDescriptorHandleForHeapStart());
        device->CreateDepthStencilView(depth_stencil_buffer, nullptr, dsv_handle);
    }

    const UINT shadowSize = 2048;

    ID3D12Resource* shadowMap = nullptr;
    ID3D12DescriptorHeap* shadowDSVHeap = nullptr;
    ID3D12DescriptorHeap* shadowSRVHeap = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE shadowDSV = {};

    {

        // Heap for writing shadow depth.
        D3D12_DESCRIPTOR_HEAP_DESC shadowDSVHeapDesc = {};
        shadowDSVHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        shadowDSVHeapDesc.NumDescriptors = 1;

        hr = device->CreateDescriptorHeap(
            &shadowDSVHeapDesc,
            IID_PPV_ARGS(&shadowDSVHeap)
        );

        if (FAILED(hr)) return 1;

        // Heap for reading the shadow map in the pixel shader.
        D3D12_DESCRIPTOR_HEAP_DESC shadowSRVHeapDesc = {};
        shadowSRVHeapDesc.Type =
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        shadowSRVHeapDesc.NumDescriptors = 5;
        shadowSRVHeapDesc.Flags =
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        hr = device->CreateDescriptorHeap(
            &shadowSRVHeapDesc,
            IID_PPV_ARGS(&shadowSRVHeap)
        );

        if (FAILED(hr)) return 1;

        // Underlying texture.
        D3D12_HEAP_PROPERTIES shadowHeapProperties = {};
        shadowHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        shadowHeapProperties.CreationNodeMask = 1;
        shadowHeapProperties.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC shadowDesc = {};
        shadowDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        shadowDesc.Width = shadowSize;
        shadowDesc.Height = shadowSize;
        shadowDesc.DepthOrArraySize = 1;
        shadowDesc.MipLevels = 1;
        shadowDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        shadowDesc.SampleDesc.Count = 1;
        shadowDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        shadowDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE shadowClear = {};
        shadowClear.Format = DXGI_FORMAT_D32_FLOAT;
        shadowClear.DepthStencil.Depth = 1.0f;

        hr = device->CreateCommittedResource(
            &shadowHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &shadowDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &shadowClear,
            IID_PPV_ARGS(&shadowMap)
        );

        if (FAILED(hr)) return 1;

        // Depth-writing view.
        D3D12_DEPTH_STENCIL_VIEW_DESC shadowDSVDesc = {};
        shadowDSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
        shadowDSVDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

        shadowDSV =
            shadowDSVHeap->GetCPUDescriptorHandleForHeapStart();

        device->CreateDepthStencilView(
            shadowMap,
            &shadowDSVDesc,
            shadowDSV
        );

        // Shader-reading view.
        D3D12_SHADER_RESOURCE_VIEW_DESC shadowSRVDesc = {};
        shadowSRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
        shadowSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        shadowSRVDesc.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        shadowSRVDesc.Texture2D.MipLevels = 1;

        device->CreateShaderResourceView(
            shadowMap,
            &shadowSRVDesc,
            shadowSRVHeap->GetCPUDescriptorHandleForHeapStart()
        );
    }



    //fence is used to synchronize the CPU with the GPU, so they don't touch the same memory at the same time
    ID3D12Fence* fence = nullptr;
    UINT64 fence_value = 0;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));

    HANDLE fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	//New Root Parameter for the shadow map. This is a descriptor table that will hold the shadow map SRV.
    D3D12_DESCRIPTOR_RANGE shadowRange = {};
    shadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadowRange.NumDescriptors = 1;
    shadowRange.BaseShaderRegister = 0; // t0
    shadowRange.RegisterSpace = 0;
    shadowRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE materialRange = {};
    materialRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    materialRange.NumDescriptors = 4;
    materialRange.BaseShaderRegister = 1; // t1 through t4
    materialRange.RegisterSpace = 0;
    materialRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER root_parameters[3] = {};

    //Root signature is like have many object buffers and textures we want to use when drawing.
   //For our rotating triangle, we only need a single constant that is going to be our angle
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[0].Constants.Num32BitValues = drawConstantCount;
    root_parameters[0].Constants.ShaderRegister = 0;
    root_parameters[0].Constants.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	//Shadow map root parameter. This is a descriptor table that will hold the shadow map SRV.
	root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
	root_parameters[1].DescriptorTable.pDescriptorRanges = &shadowRange;
    root_parameters[1].ShaderVisibility =  D3D12_SHADER_VISIBILITY_PIXEL;

    //Materials
    root_parameters[2].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[2].DescriptorTable.pDescriptorRanges = &materialRange;
    root_parameters[2].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;
    
    // Comparison sampler at s0.
    D3D12_STATIC_SAMPLER_DESC shadowSampler = {};
    shadowSampler.Filter =
        D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    shadowSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSampler.MaxAnisotropy = 1;
    shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    shadowSampler.BorderColor =
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    shadowSampler.MinLOD = 0.0f;
    shadowSampler.MaxLOD = D3D12_FLOAT32_MAX;
    shadowSampler.ShaderRegister = 0;
    shadowSampler.RegisterSpace = 0;
    shadowSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
    staticSamplers[0] = shadowSampler;

    D3D12_STATIC_SAMPLER_DESC& materialSampler =
        staticSamplers[1];

    materialSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    materialSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    materialSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    materialSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    materialSampler.MaxAnisotropy = 1;
    materialSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    materialSampler.MinLOD = 0.0f;
    materialSampler.MaxLOD = D3D12_FLOAT32_MAX;
    materialSampler.ShaderRegister = 1;
    materialSampler.RegisterSpace = 0;
    materialSampler.ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;

    ID3D12RootSignature* root_signature = nullptr;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc = {};
    root_signature_desc.NumParameters = _countof(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = _countof(staticSamplers);
    root_signature_desc.pStaticSamplers = staticSamplers;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* signature_blob = nullptr;
    ID3DBlob* error_blob = nullptr;
    hr = D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error_blob);
    hr = device->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature));

    if (signature_blob) {
        signature_blob->Release();
        signature_blob = nullptr;
    }
    if (error_blob) {
        error_blob->Release();
        error_blob = nullptr;
    }


    //compile shaders
    ID3DBlob* vertex_shader = nullptr;
    ID3DBlob* pixel_shader = nullptr;
	ID3DBlob* shadow_vertex_shader = nullptr;

    const auto compile_shader = [hwnd](const char* entry_point, const char* target, ID3DBlob** shader) {
        ID3DBlob* compiler_errors = nullptr;
        const HRESULT compile_hr = D3DCompileFromFile(
            L"shader.hlsl", nullptr, nullptr, entry_point, target,
            0, 0, shader, &compiler_errors);

        const bool succeeded = SUCCEEDED(compile_hr) && *shader != nullptr;
        if (!succeeded) {
            std::ostringstream message;
            message << "Failed to compile shader.hlsl: " << entry_point
                << " (" << target << ").\nHRESULT: 0x"
                << std::hex << static_cast<unsigned long>(compile_hr) << "\n\n";
            if (compiler_errors) {
                const std::string diagnostics(
                    static_cast<const char*>(compiler_errors->GetBufferPointer()),
                    compiler_errors->GetBufferSize());
                message << diagnostics;
            }
            else {
                message << "No compiler diagnostics were returned. Check that shader.hlsl "
                    "exists in the application's working directory.";
            }
            const std::string diagnostic_message = message.str();
            OutputDebugStringA(diagnostic_message.c_str());
            MessageBoxA(hwnd, diagnostic_message.c_str(), "Shader compilation failed", MB_OK | MB_ICONERROR);
        }
        if (compiler_errors) {
            compiler_errors->Release();
        }
        return succeeded;
    };

    ID3DBlob* skyVertexShader = nullptr;
    ID3DBlob* skyPixelShader = nullptr;

    if (
        !compile_shader(
            "SkyVS", "vs_5_0", &skyVertexShader
        ) ||
        !compile_shader(
            "SkyPS", "ps_5_0", &skyPixelShader
        )
        )
    {
        if (skyVertexShader) skyVertexShader->Release();
        if (skyPixelShader) skyPixelShader->Release();

        return 1;
    }

    if (!compile_shader("VSMain", "vs_5_0", &vertex_shader) ||
        !compile_shader("PSMain", "ps_5_0", &pixel_shader) ||
        !compile_shader("ShadowVS", "vs_5_0", &shadow_vertex_shader) ){
        if (vertex_shader) vertex_shader->Release();
        if (pixel_shader) pixel_shader->Release();
        if (shadow_vertex_shader) shadow_vertex_shader->Release();
        return 1;
    }

    // Pipeline state
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.pRootSignature = root_signature;
    pso_desc.VS.pShaderBytecode = vertex_shader->GetBufferPointer();
    pso_desc.VS.BytecodeLength = vertex_shader->GetBufferSize();
    pso_desc.PS.pShaderBytecode = pixel_shader->GetBufferPointer();
    pso_desc.PS.BytecodeLength = pixel_shader->GetBufferSize();
    set_blend_state(pso_desc.BlendState);
    pso_desc.SampleMask = UINT_MAX;
    set_rasterizer_state(pso_desc.RasterizerState);
    set_depth_stencil_state(pso_desc.DepthStencilState);
    D3D12_INPUT_ELEMENT_DESC input_elements[] = {
           {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
           {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
           {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    pso_desc.InputLayout.pInputElementDescs = input_elements;
    pso_desc.InputLayout.NumElements = _countof(input_elements);
    pso_desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.SampleDesc.Count = 1;
    pso_desc.SampleDesc.Quality = 0;

    ID3D12PipelineState* pipeline_state = nullptr;
    hr = device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state));

    if (FAILED(hr)) return 1;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPSODesc = pso_desc;

    shadowPSODesc.VS.pShaderBytecode =
        shadow_vertex_shader->GetBufferPointer();
    shadowPSODesc.VS.BytecodeLength =
        shadow_vertex_shader->GetBufferSize();

    shadowPSODesc.PS = {};
    shadowPSODesc.NumRenderTargets = 0;

    for (DXGI_FORMAT& format : shadowPSODesc.RTVFormats) {
        format = DXGI_FORMAT_UNKNOWN;
    }

    shadowPSODesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    // Start without culling so thin geometry can cast shadows.
    shadowPSODesc.RasterizerState.CullMode =
        D3D12_CULL_MODE_NONE;

    // Initial slope bias; tune after verifying the shadow.
    shadowPSODesc.RasterizerState.DepthBias = 0;
    shadowPSODesc.RasterizerState.SlopeScaledDepthBias = 1.0f;

    ID3D12PipelineState* shadowPipeline = nullptr;

    hr = device->CreateGraphicsPipelineState(
        &shadowPSODesc,
        IID_PPV_ARGS(&shadowPipeline)
    );

    if (FAILED(hr)) return 1;



    shadow_vertex_shader->Release();
    shadow_vertex_shader = nullptr;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC skyDesc = pso_desc;

    skyDesc.VS = {
        skyVertexShader->GetBufferPointer(),
        skyVertexShader->GetBufferSize()
    };

    skyDesc.PS = {
        skyPixelShader->GetBufferPointer(),
        skyPixelShader->GetBufferSize()
    };

    // SkyVS generates positions itself.
    skyDesc.InputLayout = { nullptr, 0 };

    // The background must not affect scene depth.
    skyDesc.DepthStencilState.DepthEnable = FALSE;
    skyDesc.DepthStencilState.DepthWriteMask =
        D3D12_DEPTH_WRITE_MASK_ZERO;
    skyDesc.DepthStencilState.StencilEnable = FALSE;

    skyDesc.RasterizerState.CullMode =
        D3D12_CULL_MODE_NONE;

    ID3D12PipelineState* skyPipeline = nullptr;

    hr = device->CreateGraphicsPipelineState(
        &skyDesc,
        IID_PPV_ARGS(&skyPipeline)
    );

    skyVertexShader->Release();
    skyPixelShader->Release();

    if (FAILED(hr)) return 1;
  
    vertex_shader->Release();
    vertex_shader = nullptr;
    pixel_shader->Release();
    pixel_shader = nullptr;

    Model model = load_model_from_obj("small_lpg_tank_4k.obj");
    if (model.vertices.empty() || model.indicies.empty()) {
        MessageBoxW(hwnd, L"Could not load low_turtle.obj. Check the working directory and OBJ position/normal indices.",
            L"Model loading failed", MB_OK | MB_ICONERROR);
        return 1;
    }
    ID3D12Resource* vertex_buffer = nullptr; //fast. GPU access only
    ID3D12Resource* vertex_buffer_upload = nullptr; //slow. CPU and GPU access
    ID3D12Resource* index_buffer = nullptr; //fast. GPU access only
    ID3D12Resource* index_buffer_upload = nullptr; //slow. CPU and GPU access


    Model plane = load_model_from_obj("plane.obj");
    const UINT modelIndexCount =
        static_cast<UINT>(model.indicies.size());

    const UINT planeIndexCount =
        static_cast<UINT>(plane.indicies.size());

    const UINT planeStartIndex =
        static_cast<UINT>(model.indicies.size());

    const INT planeBaseVertex =
        static_cast<INT>(model.vertices.size());

    model.vertices.insert(
        model.vertices.end(),
        plane.vertices.begin(),
        plane.vertices.end()
    );

    model.indicies.insert(
        model.indicies.end(),
        plane.indicies.begin(),
        plane.indicies.end()
    );

    {
        //heap properties
        D3D12_HEAP_PROPERTIES heap_properties = {};
        heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap_properties.CreationNodeMask = 1;
        heap_properties.VisibleNodeMask = 1;

        D3D12_HEAP_PROPERTIES heap_properties_upload = heap_properties;
        heap_properties_upload.Type = D3D12_HEAP_TYPE_UPLOAD;

        //resource description
        D3D12_RESOURCE_DESC resource_desc = {};
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resource_desc.Alignment = 0;
        resource_desc.Width = sizeof(Vertex) * model.vertices.size();
        resource_desc.Height = 1;
        resource_desc.DepthOrArraySize = 1;
        resource_desc.MipLevels = 1;
        resource_desc.Format = DXGI_FORMAT_UNKNOWN;
        resource_desc.SampleDesc.Count = 1;
        resource_desc.SampleDesc.Quality = 0;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;


        //vertex
        hr = device->CreateCommittedResource(
            &heap_properties,
            D3D12_HEAP_FLAG_NONE,
            &resource_desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&vertex_buffer)
        );

        hr = device->CreateCommittedResource(
            &heap_properties_upload,
            D3D12_HEAP_FLAG_NONE,
            &resource_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&vertex_buffer_upload)
        );

        if (FAILED(hr)) {
            MessageBoxW(hwnd, L"Vertex shader compilation failed.",
                L"Error", MB_OK);
            return 1;
        }


        //index
        resource_desc.Width = sizeof(unsigned int) * model.indicies.size();
        hr = device->CreateCommittedResource(
            &heap_properties,
            D3D12_HEAP_FLAG_NONE,
            &resource_desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&index_buffer)
        );

        hr = device->CreateCommittedResource(
            &heap_properties_upload,
            D3D12_HEAP_FLAG_NONE,
            &resource_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&index_buffer_upload)
        );

        if (FAILED(hr)) {
            MessageBoxW(hwnd, L"Vertex shader compilation failed.",
                L"Error", MB_OK);
            return 1;
        }

        //copy data from CPU to the upload buffers
        void* vertex_mapped_data = nullptr;
        hr = vertex_buffer_upload->Map(0, nullptr, &vertex_mapped_data);
        memcpy(vertex_mapped_data, model.vertices.data(), sizeof(Vertex) * model.vertices.size());
        vertex_buffer_upload->Unmap(0, nullptr);

        void* index_mapped_data = nullptr;
        hr = index_buffer_upload->Map(0, nullptr, &index_mapped_data);
        memcpy(index_mapped_data, model.indicies.data(), sizeof(unsigned int) * model.indicies.size());
        index_buffer_upload->Unmap(0, nullptr);

        //Record commands to copy the data from the upload buffer to the fast default buffer
        hr = command_allocator->Reset();
        hr = command_list->Reset(command_allocator, nullptr);

        D3D12_RESOURCE_BARRIER barrier[2] = {};
        barrier[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier[0].Transition.pResource = vertex_buffer;
        barrier[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrier[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        barrier[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier[1].Transition.pResource = index_buffer;
        barrier[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrier[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        command_list->ResourceBarrier(2, barrier);

        //copy the data from upload to the fast default buffer
        command_list->CopyResource(vertex_buffer, vertex_buffer_upload);
        command_list->CopyResource(index_buffer, index_buffer_upload);

        barrier[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;

        barrier[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;

        command_list->ResourceBarrier(2, barrier);

        hr = command_list->Close();

        ID3D12CommandList* command_lists[] = { command_list };
        command_queue->ExecuteCommandLists(1, command_lists);

        // Wait on the CPU for the GPU frame to finish
        const UINT64 current_fence_value = ++fence_value;
        hr = command_queue->Signal(fence, current_fence_value);

        if (fence->GetCompletedValue() < current_fence_value) {
            hr = fence->SetEventOnCompletion(current_fence_value, fence_event);
            WaitForSingleObject(fence_event, INFINITE);
        }
    }

    using namespace DirectX;

    const XMVECTOR lightPosition = XMVectorSet(-15, 25, -20, 1);
    const XMVECTOR lightTarget = XMVectorSet(0, -3, 0, 1);
    const XMVECTOR lightUp = XMVectorSet(0, 1, 0, 0);

    XMMATRIX lightView = XMMatrixLookAtLH(
        lightPosition,
        lightTarget,
        lightUp
    );

    XMMATRIX lightProjection = XMMatrixOrthographicLH(
        50.0f,
        50.0f,
        0.1f,
        100.0f
    );

    XMFLOAT4X4 lightVP;

    XMStoreFloat4x4(
        &lightVP,
        lightView* lightProjection
    );

    XMFLOAT3 lightDirection;

    XMStoreFloat3(
        &lightDirection,
        XMVector3Normalize(lightPosition - lightTarget)
    );

    // Views cover the combined model + plane geometry.
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view = {};
    vertex_buffer_view.BufferLocation =
        vertex_buffer->GetGPUVirtualAddress();
    vertex_buffer_view.StrideInBytes = sizeof(Vertex);
    vertex_buffer_view.SizeInBytes =
        static_cast<UINT>(sizeof(Vertex) * model.vertices.size());

    D3D12_INDEX_BUFFER_VIEW index_buffer_view = {};
    index_buffer_view.BufferLocation =
        index_buffer->GetGPUVirtualAddress();
    index_buffer_view.SizeInBytes =
        static_cast<UINT>(
            sizeof(unsigned int) * model.indicies.size()
            );
    index_buffer_view.Format = DXGI_FORMAT_R32_UINT;

    modelMaterial.BaseColor = { 1.0f, 1.0f, 1.0f };
    modelMaterial.roughness = 1.0f;
    modelMaterial.metallic = 1.0f;

    planeMaterial.BaseColor = { 0.5f, 0.5f, 0.5f };
    planeMaterial.roughness = 0.5f;
    planeMaterial.metallic = 0.0f;

    //Textures

    Microsoft::WRL::ComPtr<ID3D12Resource> modelBaseColorTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> modelRoughnessTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> modelNormalTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> modelMetallicTexture;

    {
        DirectX::ResourceUploadBatch textureUpload(device);
        textureUpload.Begin();

        const auto colorFlags =
            static_cast<DirectX::WIC_LOADER_FLAGS>(
                DirectX::WIC_LOADER_FORCE_SRGB |
                DirectX::WIC_LOADER_MIP_AUTOGEN
                );

        const auto dataFlags =
            static_cast<DirectX::WIC_LOADER_FLAGS>(
                DirectX::WIC_LOADER_IGNORE_SRGB |
                DirectX::WIC_LOADER_MIP_AUTOGEN
                );

        hr = DirectX::CreateWICTextureFromFileEx(
            device,
            textureUpload,
            L"small_lpg_tank_diff_4k.png",
            0,
            D3D12_RESOURCE_FLAG_NONE,
            colorFlags,
            modelBaseColorTexture.GetAddressOf()
        );
        if (FAILED(hr)) return 1;

        hr = DirectX::CreateWICTextureFromFileEx(
            device,
            textureUpload,
            L"small_lpg_tank_rough_4k.png",
            0,
            D3D12_RESOURCE_FLAG_NONE,
            dataFlags,
            modelRoughnessTexture.GetAddressOf()
        );
        if (FAILED(hr)) return 1;

        hr = DirectX::CreateWICTextureFromFileEx(
            device,
            textureUpload,
            L"small_lpg_tank_nor_gl_4k.png",
            0,
            D3D12_RESOURCE_FLAG_NONE,
            dataFlags,
            modelNormalTexture.GetAddressOf()
        );
        if (FAILED(hr)) return 1;

        hr = DirectX::CreateWICTextureFromFileEx(
            device,
            textureUpload,
            L"small_lpg_tank_metal_4k.png",
            0,
            D3D12_RESOURCE_FLAG_NONE,
            dataFlags,
            modelMetallicTexture.GetAddressOf()
        );
        if (FAILED(hr)) return 1;

        auto finished = textureUpload.End(command_queue);
        finished.get();
    }

    const UINT srvDescriptorSize =
        device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );

    const auto cpuSRV = [&](UINT slot) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            shadowSRVHeap->GetCPUDescriptorHandleForHeapStart();

        handle.ptr +=
            static_cast<SIZE_T>(slot) * srvDescriptorSize;

        return handle;
        };

    const auto gpuSRV = [&](UINT slot) {
        D3D12_GPU_DESCRIPTOR_HANDLE handle =
            shadowSRVHeap->GetGPUDescriptorHandleForHeapStart();

        handle.ptr +=
            static_cast<UINT64>(slot) * srvDescriptorSize;

        return handle;
        };

    device->CreateShaderResourceView(
        modelBaseColorTexture.Get(), nullptr, cpuSRV(1)
    );

    device->CreateShaderResourceView(
        modelRoughnessTexture.Get(), nullptr, cpuSRV(2)
    );

    device->CreateShaderResourceView(
        modelNormalTexture.Get(), nullptr, cpuSRV(3)
    );

    device->CreateShaderResourceView(
        modelMetallicTexture.Get(), nullptr, cpuSRV(4)
    );

    unsigned int triangle_angle = 0;
    bool running = true;

    const auto transition = [&](
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after)
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter = after;
            barrier.Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            command_list->ResourceBarrier(1, &barrier);
        };

    const auto drawScene = [&]()
        {
            command_list->IASetPrimitiveTopology(
                D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
            );

            command_list->IASetVertexBuffers(
                0, 1, &vertex_buffer_view
            );
            command_list->IASetIndexBuffer(&index_buffer_view);

            DrawConstants constants{};
            constants.angle = triangle_angle;
            constants.baseColor = modelMaterial.BaseColor;
            constants.roughness = modelMaterial.roughness;
            constants.metallic = modelMaterial.metallic;
            constants.objectScale = { 20.0f, 20.0f, 20.0f };
            constants.objectPosition = { 0, -5.0f, 0 };
            constants.lightViewProjection = lightVP;
            constants.lightDirectionWorld = lightDirection;
            constants.textureFlags = 15;
            constants.camera = camera.GetShaderData();

            command_list->SetGraphicsRoot32BitConstants(
                0, drawConstantCount, &constants, 0
            );

            command_list->DrawIndexedInstanced(
                modelIndexCount, 1, 0, 0, 0
            );

            DrawConstants planeConstants{};
            planeConstants.angle = 0;
            planeConstants.baseColor = planeMaterial.BaseColor;
            planeConstants.roughness = planeMaterial.roughness;
            planeConstants.metallic = planeMaterial.metallic;
            planeConstants.objectScale = { 0.5f, 1.0f, 0.5f };
            planeConstants.objectPosition = { 0, -5.0f, 0 };
            planeConstants.lightViewProjection = lightVP;
            planeConstants.lightDirectionWorld = lightDirection;
            planeConstants.textureFlags = 0;
            planeConstants.camera = camera.GetShaderData();

            command_list->SetGraphicsRoot32BitConstants(
                0, drawConstantCount, &planeConstants, 0
            );

            command_list->DrawIndexedInstanced(
                planeIndexCount, 1,
                planeStartIndex, planeBaseVertex, 0
            );
        };

    auto previousFrameTime = std::chrono::steady_clock::now();
    while (running) {
        MSG msg = {};
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

		if (!running) {
            break;
		}

        const auto frameTime = std::chrono::steady_clock::now();
        const float deltaSeconds = std::chrono::duration<float>(
            frameTime - previousFrameTime).count();
        previousFrameTime = frameTime;
        camera.Update(deltaSeconds);

        // Record the shadow and camera passes after updating input once.
        hr = command_allocator->Reset();
        hr = command_list->Reset(command_allocator, nullptr);

        UINT back_buffer_index = swap_chain->GetCurrentBackBufferIndex();

        transition(
            shadowMap,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE
        );

        D3D12_VIEWPORT shadowViewport = {
            0.0f, 0.0f,
            static_cast<float>(shadowSize),
            static_cast<float>(shadowSize),
            0.0f, 1.0f
        };

        D3D12_RECT shadowScissor = {
            0, 0,
            static_cast<LONG>(shadowSize),
            static_cast<LONG>(shadowSize)
        };

        command_list->RSSetViewports(1, &shadowViewport);
        command_list->RSSetScissorRects(1, &shadowScissor);

        command_list->SetGraphicsRootSignature(root_signature);
        command_list->SetPipelineState(shadowPipeline);

        command_list->OMSetRenderTargets(
            0, nullptr, FALSE, &shadowDSV
        );

        command_list->ClearDepthStencilView(
            shadowDSV,
            D3D12_CLEAR_FLAG_DEPTH,
            1.0f,
            0,
            0,
            nullptr
        );

        drawScene();

        transition(
            shadowMap,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );

        // --------------------------------------------------
        // Pass 2: render the camera image.
        // --------------------------------------------------

        transition(
            render_targets[back_buffer_index],
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );

        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
            rtv_heap->GetCPUDescriptorHandleForHeapStart();

        rtv_handle.ptr +=
            back_buffer_index * rtv_increment_size;

        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle =
            dsv_heap->GetCPUDescriptorHandleForHeapStart();

        float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };

        command_list->ClearRenderTargetView(
            rtv_handle, clearColor, 0, nullptr
        );

        command_list->ClearDepthStencilView(
            dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH,
            1.0f,
            0,
            0,
            nullptr
        );

        // Restore the camera-sized viewport.
        D3D12_VIEWPORT viewport = {
            0.0f, 0.0f,
            static_cast<float>(width),
            static_cast<float>(height),
            0.0f, 1.0f
        };

        D3D12_RECT scissor_rect = { 0, 0, width, height };

        command_list->RSSetViewports(1, &viewport);
        command_list->RSSetScissorRects(1, &scissor_rect);

        command_list->OMSetRenderTargets(
            1, &rtv_handle, FALSE, &dsv_handle
        );

        command_list->SetGraphicsRootSignature(root_signature);

        command_list->SetPipelineState(skyPipeline);

        command_list->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
        );

        command_list->DrawInstanced(3, 1, 0, 0);

        command_list->SetPipelineState(pipeline_state);

        ID3D12DescriptorHeap* shaderHeaps[] = {
            shadowSRVHeap
        };

        command_list->SetDescriptorHeaps(
            _countof(shaderHeaps),
            shaderHeaps
        );

        command_list->SetGraphicsRootDescriptorTable(
            1,
            shadowSRVHeap->GetGPUDescriptorHandleForHeapStart()
        );
        command_list->SetGraphicsRootDescriptorTable(
            2,
            gpuSRV(1)
        );

        drawScene();

        transition(
            render_targets[back_buffer_index],
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT
        );

        hr = command_list->Close();

        ID3D12CommandList* command_lists[] = { command_list };
        command_queue->ExecuteCommandLists(1, command_lists);

        hr = swap_chain->Present(1, 0);

        // Wait on the CPU for the GPU frame to finish
        const UINT64 current_fence_value = ++fence_value;
        hr = command_queue->Signal(fence, current_fence_value);

        if (fence->GetCompletedValue() < current_fence_value) {
            hr = fence->SetEventOnCompletion(current_fence_value, fence_event);
            WaitForSingleObject(fence_event, INFINITE);
        }

        triangle_angle++;
    }

    shadowPipeline->Release();
    shadowMap->Release();
    shadowDSVHeap->Release();
    shadowSRVHeap->Release();

    return 0;
}
