#include "SharedSurfaceProtocol.h"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
struct Slot {
    ComPtr<ID3D11Texture2D> producerTexture;
    ComPtr<ID3D11RenderTargetView> producerTarget;
    ComPtr<IDXGIKeyedMutex> producerMutex;
    ComPtr<ID3D11Texture2D> consumerTexture;
    ComPtr<IDXGIKeyedMutex> consumerMutex;
    std::uint64_t sharedToken{};
};

bool createDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE type,
                  ComPtr<ID3D11Device>& device, ComPtr<ID3D11DeviceContext>& context)
{
    constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL selected{};
    auto hr = D3D11CreateDevice(adapter, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
        &device, &selected, &context);
    if (hr == E_INVALIDARG) {
        hr = D3D11CreateDevice(adapter, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels + 1, static_cast<UINT>(std::size(levels) - 1), D3D11_SDK_VERSION,
            &device, &selected, &context);
    }
    if (FAILED(hr))
        std::wcerr << L"FAIL: D3D11CreateDevice HRESULT=0x" << std::hex << hr << L"\n";
    return SUCCEEDED(hr);
}

bool createRing(ID3D11Device* producer, ID3D11Device* consumer,
                std::uint32_t width, std::uint32_t height, std::array<Slot, 3>& slots,
                Blaze::Gpu::SharedSurfaceDescriptor& descriptor, std::uint64_t generation)
{
    using namespace Blaze::Gpu;
    descriptor = {};
    descriptor.structSize = sizeof(descriptor);
    descriptor.protocolVersion = kSurfaceProtocolVersion;
    descriptor.viewIdentifier = 0xB1A2E;
    descriptor.resizeGeneration = generation;
    descriptor.width = width;
    descriptor.height = height;
    descriptor.pixelFormat = kPixelFormatBgra8Unorm;
    descriptor.synchronizationKind = kSyncKeyedMutex;
    descriptor.slotCount = static_cast<std::uint32_t>(slots.size());
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC adapterDescription{};
    if (FAILED(producer->QueryInterface(IID_PPV_ARGS(&dxgiDevice)))
        || FAILED(dxgiDevice->GetAdapter(&adapter))
        || FAILED(adapter->GetDesc(&adapterDescription)))
        return false;
    descriptor.adapter = {
        static_cast<std::uint32_t>(adapterDescription.AdapterLuid.LowPart),
        adapterDescription.AdapterLuid.HighPart
    };

    for (std::uint32_t i = 0; i < slots.size(); ++i) {
        D3D11_TEXTURE2D_DESC textureDescription{};
        textureDescription.Width = width;
        textureDescription.Height = height;
        textureDescription.MipLevels = 1;
        textureDescription.ArraySize = 1;
        textureDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Usage = D3D11_USAGE_DEFAULT;
        textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        textureDescription.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

        auto& slot = slots[i];
        if (FAILED(producer->CreateTexture2D(&textureDescription, nullptr, &slot.producerTexture))
            || FAILED(producer->CreateRenderTargetView(slot.producerTexture.Get(), nullptr, &slot.producerTarget))
            || FAILED(slot.producerTexture.As(&slot.producerMutex)))
            return false;

        ComPtr<IDXGIResource> resource;
        HANDLE sharedHandle{};
        if (FAILED(slot.producerTexture.As(&resource))
            || FAILED(resource->GetSharedHandle(&sharedHandle)) || !sharedHandle)
            return false;
        slot.sharedToken = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(sharedHandle));

        if (FAILED(consumer->OpenSharedResource(sharedHandle, IID_PPV_ARGS(&slot.consumerTexture)))
            || FAILED(slot.consumerTexture.As(&slot.consumerMutex)))
            return false;

        descriptor.slots[i] = { slot.sharedToken, 0, 1 };
    }
    return isValid(descriptor);
}

bool exerciseGeneration(ID3D11Device* producer, ID3D11DeviceContext* producerContext,
                        ID3D11Device* consumer, ID3D11DeviceContext* consumerContext,
                        std::uint32_t width, std::uint32_t height, std::uint64_t generation,
                        Blaze::Gpu::BoundedFrameRing& state, std::uint64_t& frameSequence)
{
    using namespace Blaze::Gpu;
    std::array<Slot, 3> slots{};
    SharedSurfaceDescriptor descriptor{};
    if (!createRing(producer, consumer, width, height, slots, descriptor, generation)) {
        std::wcerr << L"FAIL: could not create/import the three-slot BGRA8 ring for generation "
                   << generation << L".\n";
        return false;
    }

    std::vector<ComPtr<ID3D11Texture2D>> consumerCopies(slots.size());
    for (auto& copy : consumerCopies) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(consumer->CreateTexture2D(&desc, nullptr, &copy)))
            return false;
    }

    if (state.slotCount() != slots.size() || !state.beginGeneration(generation))
        return false;

    for (std::uint32_t frame = 0; frame < 9; ++frame) {
        FrameTicket ticket{};
        if (!state.tryBeginRender(ticket))
            return false;
        auto& slot = slots[ticket.slotIndex];
        if (slot.producerMutex->AcquireSync(0, 10000) != S_OK) {
            std::wcerr << L"FAIL: producer could not acquire ring slot.\n";
            return false;
        }
        const float red = static_cast<float>((frame + 1) % 9) / 9.0f;
        const float green = static_cast<float>(generation % 3 + 1) / 4.0f;
        const float blue = static_cast<float>((frame + 3) % 9) / 9.0f;
        const float color[] = { red, green, blue, 1.0f };
        producerContext->ClearRenderTargetView(slot.producerTarget.Get(), color);
        producerContext->Flush();
        if (slot.producerMutex->ReleaseSync(1) != S_OK)
            return false;
        if (!state.publish(ticket))
            return false;

        FrameTicket present{};
        std::uint32_t dropped = 0;
        if (!state.tryBeginPresent(present, dropped) || dropped != 0
            || present.sequence != ticket.sequence || present.slotIndex != ticket.slotIndex)
            return false;

        if (slot.consumerMutex->AcquireSync(1, 10000) != S_OK) {
            std::wcerr << L"FAIL: consumer could not acquire completed frame.\n";
            return false;
        }
        consumerContext->CopyResource(consumerCopies[present.slotIndex].Get(), slot.consumerTexture.Get());
        consumerContext->Flush();
        if (slot.consumerMutex->ReleaseSync(2) != S_OK)
            return false;
        if (slot.producerMutex->AcquireSync(2, 10000) != S_OK)
            return false;
        if (slot.producerMutex->ReleaseSync(0) != S_OK)
            return false;
        if (state.completePresent(present) != PresentResult::Presented)
            return false;

        ++frameSequence;
        if (frameSequence != state.lastIssuedSequence())
            return false;
    }
    std::wcout << L"PASS: generation " << generation << L" (" << width << L"x" << height
               << L") rendered and GPU-copied 9 frames across three keyed-mutex shared textures.\n";
    return true;
}
}

int wmain()
{
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return 1;
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapters1(0, &adapter))) {
        std::wcerr << L"FAIL: no DXGI adapter available for shared ring test.\n";
        return 2;
    }

    ComPtr<ID3D11Device> producer, consumer;
    ComPtr<ID3D11DeviceContext> producerContext, consumerContext;
    if (!createDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, producer, producerContext)
        || !createDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, consumer, consumerContext))
        return 3;

    std::uint64_t sequence = 0;
    Blaze::Gpu::BoundedFrameRing ring(3);
    if (!exerciseGeneration(producer.Get(), producerContext.Get(), consumer.Get(), consumerContext.Get(),
                            640, 360, 1, ring, sequence))
        return 4;
    // Replace the whole ring on resize. The monotonically increasing generation
    // distinguishes the new resources from any late notifications on old ones.
    if (!exerciseGeneration(producer.Get(), producerContext.Get(), consumer.Get(), consumerContext.Get(),
                            1280, 720, 2, ring, sequence))
        return 5;

    std::wcout << L"PASS: bounded three-texture cross-device ring and resize-generation recreation; "
                  L"frame IDs issued=" << sequence << L". GPU copies only; no CPU pixel readback.\n";
    return 0;
}
