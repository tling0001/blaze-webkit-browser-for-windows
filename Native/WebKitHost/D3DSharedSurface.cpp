#include "WebKitHost.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
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

void WB_GpuSurfaceDestroy(WB_GPU_SURFACE_HANDLE surface)
{
    // The caller must first retire the WebKit GPU-process consumer and wait for
    // its generation acknowledgement. Releasing this object invalidates tokens.
    delete surface;
}
