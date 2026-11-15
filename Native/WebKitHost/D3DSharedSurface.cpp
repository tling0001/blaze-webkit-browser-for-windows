#include "WebKitHost.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.ui.xaml.media.dxinterop.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <new>

using Microsoft::WRL::ComPtr;

struct WB_GPU_SURFACE {
    struct Slot {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> renderTarget;
        ComPtr<IDXGIKeyedMutex> keyedMutex;
    };

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    std::array<Slot, Blaze::Gpu::kMaximumRingSlots> slots;
    Blaze::Gpu::SharedSurfaceDescriptor descriptor{};
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ISwapChainPanelNative> panelNative;
    std::mutex presentationMutex;
    std::uint64_t lastPresentedSequence{};
};

namespace {
bool createD3DDevice(WB_GPU_SURFACE& surface)
{
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))
        || FAILED(factory->EnumAdapters1(0, &adapter)))
        return false;

    constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL selected{};
    auto result = D3D11CreateDevice(
        adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
        static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
        &surface.device, &selected, &surface.context);
    if (result == E_INVALIDARG) {
        result = D3D11CreateDevice(
            adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels + 1,
            static_cast<UINT>(std::size(levels) - 1), D3D11_SDK_VERSION,
            &surface.device, &selected, &surface.context);
    }
    if (FAILED(result))
        return false;

    DXGI_ADAPTER_DESC adapterDescription{};
    if (FAILED(adapter->GetDesc(&adapterDescription)))
        return false;
    surface.descriptor.adapter = {
        static_cast<std::uint32_t>(adapterDescription.AdapterLuid.LowPart),
        adapterDescription.AdapterLuid.HighPart
    };
    return true;
}

bool createSharedRing(WB_GPU_SURFACE& surface)
{
    using namespace Blaze::Gpu;
    for (std::uint32_t index = 0; index < surface.descriptor.slotCount; ++index) {
        D3D11_TEXTURE2D_DESC textureDescription{};
        textureDescription.Width = surface.descriptor.width;
        textureDescription.Height = surface.descriptor.height;
        textureDescription.MipLevels = 1;
        textureDescription.ArraySize = 1;
        textureDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Usage = D3D11_USAGE_DEFAULT;
        textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        textureDescription.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

        auto& slot = surface.slots[index];
        if (FAILED(surface.device->CreateTexture2D(&textureDescription, nullptr, &slot.texture))
            || FAILED(surface.device->CreateRenderTargetView(slot.texture.Get(), nullptr, &slot.renderTarget))
            || FAILED(slot.texture.As(&slot.keyedMutex)))
            return false;

        ComPtr<IDXGIResource> resource;
        HANDLE sharedHandle{};
        if (FAILED(slot.texture.As(&resource))
            || FAILED(resource->GetSharedHandle(&sharedHandle))
            || !sharedHandle)
            return false;

        // GetSharedHandle returns a legacy DXGI shared-resource token owned by
        // the resource. It is not an NT HANDLE and must not be CloseHandle'd.
        surface.descriptor.slots[index] = {
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(sharedHandle)),
            0, 1, 2
        };
    }
    return isValid(surface.descriptor);
}
}

WB_GPU_SURFACE_HANDLE WB_GpuSurfaceCreate(
    std::uint64_t viewIdentifier, std::uint32_t width,
    std::uint32_t height, std::uint64_t generation)
{
    using namespace Blaze::Gpu;
    if (!viewIdentifier || !generation || !width || !height
        || width > kMaximumSurfaceDimension || height > kMaximumSurfaceDimension)
        return nullptr;

    auto surface = new (std::nothrow) WB_GPU_SURFACE();
    if (!surface)
        return nullptr;

    auto& descriptor = surface->descriptor;
    descriptor.structSize = sizeof(descriptor);
    descriptor.protocolVersion = kSurfaceProtocolVersion;
    descriptor.viewIdentifier = viewIdentifier;
    descriptor.resizeGeneration = generation;
    descriptor.width = width;
    descriptor.height = height;
    descriptor.pixelFormat = kPixelFormatBgra8Unorm;
    descriptor.synchronizationKind = kSyncKeyedMutex;
    descriptor.slotCount = kMaximumRingSlots;

    if (!createD3DDevice(*surface) || !createSharedRing(*surface)) {
        delete surface;
        return nullptr;
    }
    return surface;
}

BOOL WB_GpuSurfaceGetDescriptor(
    WB_GPU_SURFACE_HANDLE surface, Blaze::Gpu::SharedSurfaceDescriptor* descriptor)
{
    if (!surface || !descriptor || !Blaze::Gpu::isValid(surface->descriptor))
        return FALSE;
    *descriptor = surface->descriptor;
    return TRUE;
}

HRESULT WB_GpuSurfaceAttachSwapChainPanel(WB_GPU_SURFACE_HANDLE surface, IUnknown* panel)
{
    if (!surface || !panel)
        return E_INVALIDARG;
    std::lock_guard lock(surface->presentationMutex);
    if (surface->swapChain)
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);

    ComPtr<ISwapChainPanelNative> panelNative;
    auto result = panel->QueryInterface(IID_PPV_ARGS(&panelNative));
    if (FAILED(result))
        return result;

    ComPtr<IDXGIDevice> dxgiDevice;
    result = surface->device.As(&dxgiDevice);
    if (FAILED(result))
        return result;
    ComPtr<IDXGIAdapter> adapter;
    result = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(result))
        return result;
    ComPtr<IDXGIFactory2> factory;
    result = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(result))
        return result;

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = surface->descriptor.width;
    description.Height = surface->descriptor.height;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swapChain;
    result = factory->CreateSwapChainForComposition(
        surface->device.Get(), &description, nullptr, &swapChain);
    if (FAILED(result))
        return result;
    result = panelNative->SetSwapChain(swapChain.Get());
    if (FAILED(result))
        return result;

    surface->panelNative = std::move(panelNative);
    surface->swapChain = std::move(swapChain);
    surface->lastPresentedSequence = 0;
    return S_OK;
}

HRESULT WB_GpuSurfacePresent(
    WB_GPU_SURFACE_HANDLE surface, const Blaze::Gpu::FrameReadyMessage* frame)
{
    using namespace Blaze::Gpu;
    if (!surface || !frame || !isValid(*frame, surface->descriptor))
        return E_INVALIDARG;
    std::lock_guard lock(surface->presentationMutex);
    if (!surface->swapChain || !surface->panelNative)
        return E_HANDLE;
    if (frame->frameSequence <= surface->lastPresentedSequence)
        return S_FALSE;

    auto& slot = surface->slots[frame->slotIndex];
    const auto acquired = slot.keyedMutex->AcquireSync(frame->producerReleaseKey, 0);
    if (acquired == WAIT_TIMEOUT)
        return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    if (FAILED(acquired))
        return acquired;

    ComPtr<ID3D11Texture2D> backBuffer;
    auto result = surface->swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (SUCCEEDED(result)) {
        // Same-device BGRA8 GPU copy; no CPU readback or bitmap IPC.
        surface->context->CopyResource(backBuffer.Get(), slot.texture.Get());
        result = surface->swapChain->Present(1, 0);
    }
    const auto released = slot.keyedMutex->ReleaseSync(frame->consumerReleaseKey);
    if (FAILED(released))
        return released;
    if (SUCCEEDED(result))
        surface->lastPresentedSequence = frame->frameSequence;
    return result;
}

HRESULT WB_GpuSurfaceDetachSwapChainPanel(WB_GPU_SURFACE_HANDLE surface)
{
    if (!surface)
        return E_INVALIDARG;
    std::lock_guard lock(surface->presentationMutex);
    if (surface->panelNative) {
        const auto result = surface->panelNative->SetSwapChain(nullptr);
        if (FAILED(result))
            return result;
    }
    surface->swapChain.Reset();
    surface->panelNative.Reset();
    surface->lastPresentedSequence = 0;
    return S_OK;
}

void WB_GpuSurfaceDestroy(WB_GPU_SURFACE_HANDLE surface)
{
    if (!surface)
        return;
    (void)WB_GpuSurfaceDetachSwapChainPanel(surface);
    // Retire the WebKit GPU-process consumer and acknowledge this generation
    // before destroying the owner; shared-resource tokens die with this object.
    delete surface;
}
