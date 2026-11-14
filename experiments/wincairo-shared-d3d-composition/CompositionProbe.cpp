#include <Windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr UINT kWidth = 32;
constexpr UINT kHeight = 32;
constexpr unsigned char kExpectedPixel[4] = { 0x2A, 0x64, 0xC8, 0xFF };

void printHr(const wchar_t* operation, HRESULT hr)
{
    std::wcerr << L"FAIL: " << operation << L" HRESULT=0x"
               << std::hex << static_cast<unsigned long>(hr) << std::dec << L"\n";
}

bool createDevice(D3D_DRIVER_TYPE driverType, const LUID* requestedLuid,
                  ComPtr<ID3D11Device>& device, ComPtr<ID3D11DeviceContext>& context,
                  LUID& actualLuid)
{
    ComPtr<IDXGIAdapter1> selectedAdapter;
    if (requestedLuid && driverType == D3D_DRIVER_TYPE_UNKNOWN) {
        ComPtr<IDXGIFactory1> factory;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            printHr(L"CreateDXGIFactory1", hr);
            return false;
        }

        for (UINT index = 0; ; ++index) {
            ComPtr<IDXGIAdapter1> candidate;
            hr = factory->EnumAdapters1(index, &candidate);
            if (hr == DXGI_ERROR_NOT_FOUND)
                break;
            if (FAILED(hr)) {
                printHr(L"IDXGIFactory1::EnumAdapters1", hr);
                return false;
            }

            DXGI_ADAPTER_DESC1 description{};
            if (SUCCEEDED(candidate->GetDesc1(&description))
                && description.AdapterLuid.HighPart == requestedLuid->HighPart
                && description.AdapterLuid.LowPart == requestedLuid->LowPart) {
                selectedAdapter = candidate;
                break;
            }
        }

        if (!selectedAdapter) {
            std::wcerr << L"FAIL: the ANGLE/D3D adapter LUID could not be reopened.\n";
            return false;
        }
    }

    constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL selectedLevel{};
    const D3D_DRIVER_TYPE effectiveType = selectedAdapter ? D3D_DRIVER_TYPE_UNKNOWN : driverType;
    HRESULT hr = D3D11CreateDevice(
        selectedAdapter.Get(), effectiveType, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
        &device, &selectedLevel, &context);
    if (hr == E_INVALIDARG) {
        // Older runtimes do not accept the 11_1 feature-level token.
        hr = D3D11CreateDevice(
            selectedAdapter.Get(), effectiveType, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels + 1, static_cast<UINT>(std::size(levels) - 1), D3D11_SDK_VERSION,
            &device, &selectedLevel, &context);
    }
    if (FAILED(hr)) {
        printHr(L"D3D11CreateDevice", hr);
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC description{};
    if (FAILED(device.As(&dxgiDevice))
        || FAILED(dxgiDevice->GetAdapter(&adapter))
        || FAILED(adapter->GetDesc(&description))) {
        std::wcerr << L"FAIL: unable to query the D3D11 adapter identity.\n";
        return false;
    }
    actualLuid = description.AdapterLuid;
    std::wcout << L"D3D11 adapter LUID: "
               << std::hex << static_cast<unsigned long>(actualLuid.HighPart) << L":"
               << actualLuid.LowPart << std::dec
               << L", feature level 0x" << std::hex << selectedLevel << std::dec << L"\n";
    return true;
}

int consumeSharedTexture(HANDLE sharedHandle, D3D_DRIVER_TYPE driverType, LUID requestedLuid)
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    LUID actualLuid{};
    if (!createDevice(driverType, driverType == D3D_DRIVER_TYPE_UNKNOWN ? &requestedLuid : nullptr,
                      device, context, actualLuid))
        return 10;

    if (actualLuid.HighPart != requestedLuid.HighPart || actualLuid.LowPart != requestedLuid.LowPart) {
        std::wcerr << L"FAIL: child D3D device selected a different adapter.\n";
        return 11;
    }

    ComPtr<ID3D11Texture2D> sharedTexture;
    HRESULT hr = device->OpenSharedResource(sharedHandle, IID_PPV_ARGS(&sharedTexture));
    if (FAILED(hr)) {
        printHr(L"ID3D11Device::OpenSharedResource (legacy DXGI handle)", hr);
        return 13;
    }

    D3D11_TEXTURE2D_DESC stagingDescription{};
    sharedTexture->GetDesc(&stagingDescription);
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDescription.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    hr = device->CreateTexture2D(&stagingDescription, nullptr, &staging);
    if (FAILED(hr)) {
        printHr(L"Create staging texture", hr);
        return 16;
    }

    context->CopyResource(staging.Get(), sharedTexture.Get());
    context->Flush();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        printHr(L"Map staging texture", hr);
        return 17;
    }

    const auto* pixel = static_cast<const unsigned char*>(mapped.pData);
    const bool matches = std::equal(std::begin(kExpectedPixel), std::end(kExpectedPixel), pixel);
    context->Unmap(staging.Get(), 0);

    if (!matches) {
        std::wcerr << L"FAIL: shared texture pixel did not match the known BGRA test pattern.\n";
        return 19;
    }

    std::wcout << L"PASS: child process opened the legacy DXGI shared texture and verified the GPU resource contents.\n";
    return 0;
}

bool runCrossProcessSharedTextureProbe()
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    LUID adapterLuid{};
    D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_HARDWARE;

    if (!createDevice(driverType, nullptr, device, context, adapterLuid)) {
        std::wcout << L"Hardware D3D11 device unavailable; retrying with WARP for the resource-sharing diagnostic.\n";
        device.Reset();
        context.Reset();
        driverType = D3D_DRIVER_TYPE_WARP;
        if (!createDevice(driverType, nullptr, device, context, adapterLuid))
            return false;
    }

    D3D11_TEXTURE2D_DESC description{};
    description.Width = kWidth;
    description.Height = kHeight;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    description.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = device->CreateTexture2D(&description, nullptr, &texture);
    if (FAILED(hr)) {
        printHr(L"Create NT-handle/keyed-mutex texture", hr);
        return false;
    }

    // The pinned ANGLE EGL share-handle extension opens this with
    // ID3D11Device::OpenSharedResource, so use a legacy DXGI shared handle.
    // NT handles from CreateSharedHandle require OpenSharedResource1 and import
    // as an empty surface through this ANGLE path.
    ComPtr<IDXGIResource> resource;
    hr = texture.As(&resource);
    if (FAILED(hr)) {
        printHr(L"Query IDXGIResource", hr);
        return false;
    }

    HANDLE sharedHandle = nullptr;
    hr = resource->GetSharedHandle(&sharedHandle);
    if (FAILED(hr) || !sharedHandle) {
        printHr(L"IDXGIResource::GetSharedHandle", hr);
        return false;
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(kWidth) * kHeight * 4);
    for (size_t offset = 0; offset < pixels.size(); offset += 4)
        std::copy(std::begin(kExpectedPixel), std::end(kExpectedPixel), pixels.begin() + offset);
    context->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), kWidth * 4, 0);
    context->Flush();

    std::wstring executable(MAX_PATH, L'\0');
    const DWORD executableLength = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!executableLength || executableLength >= executable.size()) {
                std::wcerr << L"FAIL: unable to resolve the probe executable path.\n";
        return false;
    }
    executable.resize(executableLength);

    // The pipe transfers the legacy DXGI share-handle token as a numeric value.
    // It is not an NT handle: do not DuplicateHandle or CloseHandle it. The
    // producer texture remains alive until the child has opened and read it.
    SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE pipeRead = nullptr;
    HANDLE pipeWrite = nullptr;
    if (!CreatePipe(&pipeRead, &pipeWrite, &security, 0)
        || !SetHandleInformation(pipeWrite, HANDLE_FLAG_INHERIT, 0)) {
        if (pipeRead) CloseHandle(pipeRead);
        if (pipeWrite) CloseHandle(pipeWrite);
                std::wcerr << L"FAIL: CreatePipe error=" << GetLastError() << L"\n";
        return false;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + executable + L"\" --consume "
        + std::to_wstring(reinterpret_cast<uintptr_t>(pipeRead)) + L" "
        + std::to_wstring(adapterLuid.HighPart) + L" "
        + std::to_wstring(adapterLuid.LowPart) + L" "
        + std::to_wstring(static_cast<int>(
            driverType == D3D_DRIVER_TYPE_HARDWARE ? D3D_DRIVER_TYPE_UNKNOWN : driverType));
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    if (!CreateProcessW(executable.c_str(), mutableCommandLine.data(), nullptr, nullptr, TRUE,
                        CREATE_SUSPENDED, nullptr, nullptr, &startup, &process)) {
        CloseHandle(pipeRead);
        CloseHandle(pipeWrite);
                std::wcerr << L"FAIL: CreateProcessW error=" << GetLastError() << L"\n";
        return false;
    }
    CloseHandle(pipeRead);

    const std::string sharedHandleText = std::to_string(reinterpret_cast<uintptr_t>(sharedHandle));
    DWORD bytesWritten = 0;
    const BOOL sent = WriteFile(pipeWrite, sharedHandleText.data(),
                                static_cast<DWORD>(sharedHandleText.size()), &bytesWritten, nullptr);
    CloseHandle(pipeWrite);
    if (!sent || bytesWritten != sharedHandleText.size()) {
        std::wcerr << L"FAIL: sending legacy DXGI shared-handle token to child failed. Error=" << GetLastError() << L"\n";
        TerminateProcess(process.hProcess, 21);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
                return false;
    }

    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    const DWORD waitResult = WaitForSingleObject(process.hProcess, 30000);
    DWORD childExitCode = 0xFFFFFFFF;
    if (waitResult == WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess, &childExitCode);
    else {
        TerminateProcess(process.hProcess, 22);
        std::wcerr << L"FAIL: child shared-texture probe timed out.\n";
    }

    CloseHandle(process.hProcess);
        if (waitResult != WAIT_OBJECT_0 || childExitCode != 0) {
        std::wcerr << L"FAIL: child shared-texture probe exit code=" << childExitCode << L"\n";
        return false;
    }

    std::wcout << L"PASS: cross-process D3D11 legacy DXGI shared-handle transport.\n";
    return true;
}

using EGLDisplay = void*;
using EGLSurface = void*;
using EGLContext = void*;
using EGLDeviceEXT = void*;
using EGLBoolean = unsigned int;
using EGLint = int;
using EGLAttrib = intptr_t;

constexpr EGLint EGL_NONE_VALUE = 0x3038;
constexpr EGLint EGL_VENDOR_VALUE = 0x3053;
constexpr EGLint EGL_VERSION_VALUE = 0x3054;
constexpr EGLint EGL_EXTENSIONS_VALUE = 0x3055;
constexpr EGLint EGL_DEVICE_EXT_VALUE = 0x322C;
constexpr EGLint EGL_D3D11_DEVICE_ANGLE_VALUE = 0x33A1;
constexpr EGLint EGL_PLATFORM_ANGLE_VALUE = 0x3202;
constexpr EGLint EGL_PLATFORM_ANGLE_TYPE_VALUE = 0x3203;
constexpr EGLint EGL_PLATFORM_ANGLE_TYPE_D3D11_VALUE = 0x3208;
constexpr EGLint EGL_PLATFORM_ANGLE_DEVICE_TYPE_VALUE = 0x3209;
constexpr EGLint EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_VALUE = 0x320A;

using EglGetProcAddress = void* (WINAPI*)(const char*);
using EglGetPlatformDisplay = EGLDisplay (WINAPI*)(EGLint, void*, const EGLint*);
using EglGetDisplay = EGLDisplay (WINAPI*)(void*);
using EglInitialize = EGLBoolean (WINAPI*)(EGLDisplay, EGLint*, EGLint*);
using EglQueryString = const char* (WINAPI*)(EGLDisplay, EGLint);
using EglTerminate = EGLBoolean (WINAPI*)(EGLDisplay);
using EglQueryDisplayAttrib = EGLBoolean (WINAPI*)(EGLDisplay, EGLint, EGLAttrib*);
using EglQueryDeviceAttrib = EGLBoolean (WINAPI*)(EGLDeviceEXT, EGLint, EGLAttrib*);
using EglQueryDeviceString = const char* (WINAPI*)(EGLDeviceEXT, EGLint);

bool hasExtension(const char* extensions, const char* target)
{
    if (!extensions || !target)
        return false;
    const std::string list(extensions);
    const std::string token(target);
    size_t position = 0;
    while ((position = list.find(token, position)) != std::string::npos) {
        const bool leftBoundary = position == 0 || list[position - 1] == ' ';
        const size_t end = position + token.size();
        const bool rightBoundary = end == list.size() || list[end] == ' ';
        if (leftBoundary && rightBoundary)
            return true;
        position = end;
    }
    return false;
}

bool presentWithDirectComposition(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source)
{
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC",
        L"Blaze DirectComposition Probe", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 320, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!window) {
        std::wcerr << L"FAIL: could not create the DirectComposition probe HWND. Error=" << GetLastError() << L"\n";
        return false;
    }
    auto fail = [&](const wchar_t* operation, HRESULT hr) {
        printHr(operation, hr);
        DestroyWindow(window);
        return false;
    };
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr)) return fail(L"Query IDXGIDevice for DirectComposition", hr);
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return fail(L"Get DXGI adapter for DirectComposition", hr);
    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return fail(L"Get IDXGIFactory2 for DirectComposition", hr);
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = kWidth;
    description.Height = kHeight;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    ComPtr<IDXGISwapChain1> swapChain;
    hr = factory->CreateSwapChainForComposition(device, &description, nullptr, &swapChain);
    if (FAILED(hr)) return fail(L"CreateSwapChainForComposition", hr);
    ComPtr<IDCompositionDevice> compositionDevice;
    hr = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&compositionDevice));
    if (FAILED(hr)) return fail(L"DCompositionCreateDevice", hr);
    ComPtr<IDCompositionTarget> target;
    hr = compositionDevice->CreateTargetForHwnd(window, FALSE, &target);
    if (FAILED(hr)) return fail(L"IDCompositionDevice::CreateTargetForHwnd", hr);
    ComPtr<IDCompositionVisual> visual;
    hr = compositionDevice->CreateVisual(&visual);
    if (FAILED(hr)) return fail(L"IDCompositionDevice::CreateVisual", hr);
    hr = visual->SetContent(swapChain.Get());
    if (FAILED(hr)) return fail(L"IDCompositionVisual::SetContent", hr);
    hr = target->SetRoot(visual.Get());
    if (FAILED(hr)) return fail(L"IDCompositionTarget::SetRoot", hr);
    hr = compositionDevice->Commit();
    if (FAILED(hr)) return fail(L"Initial DirectComposition Commit", hr);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return fail(L"Get DirectComposition swap-chain back buffer", hr);

    ComPtr<IDXGIKeyedMutex> sourceMutex;
    hr = source->QueryInterface(IID_PPV_ARGS(&sourceMutex));
    if (FAILED(hr) || !sourceMutex)
        return fail(L"Query keyed mutex for DirectComposition source", FAILED(hr) ? hr : E_NOINTERFACE);
    hr = sourceMutex->AcquireSync(3, 10000);
    if (hr != S_OK)
        return fail(L"DirectComposition AcquireSync key 3", hr);

    context->CopyResource(backBuffer.Get(), source);
    context->Flush();
    hr = sourceMutex->ReleaseSync(4);
    if (hr != S_OK)
        return fail(L"DirectComposition ReleaseSync key 4", hr);
    hr = swapChain->Present(1, 0);
    if (FAILED(hr)) return fail(L"Present ANGLE-rendered texture through DirectComposition", hr);
    hr = compositionDevice->Commit();
    if (FAILED(hr)) return fail(L"Commit DirectComposition frame", hr);
    Sleep(150);
    target->SetRoot(nullptr);
    compositionDevice->Commit();
    DestroyWindow(window);
    std::wcout << L"PASS: DirectComposition acquired keyed-mutex key 3, copied the ANGLE-rendered D3D11 texture into a composition swap chain on the GPU, and released key 4 before Present (no CPU pixel upload).\n";
    return true;
}

bool renderThroughAngleD3DTexture(EGLDisplay display, EglGetProcAddress getProcAddress,
                                 ID3D11Device* angleD3DDevice)
{
    constexpr EGLint EGL_SURFACE_TYPE_VALUE = 0x3033;
    constexpr EGLint EGL_PBUFFER_BIT_VALUE = 0x0001;
    constexpr EGLint EGL_RENDERABLE_TYPE_VALUE = 0x3040;
    constexpr EGLint EGL_OPENGL_ES2_BIT_VALUE = 0x0004;
    constexpr EGLint EGL_BIND_TO_TEXTURE_RGBA_VALUE = 0x303B;
    constexpr EGLint EGL_TEXTURE_FORMAT_VALUE = 0x3080;
    constexpr EGLint EGL_TEXTURE_TARGET_VALUE = 0x3081;
    constexpr EGLint EGL_TEXTURE_RGBA_VALUE = 0x305E;
    constexpr EGLint EGL_TEXTURE_2D_VALUE = 0x305F;
    constexpr EGLint EGL_RED_SIZE_VALUE = 0x3024;
    constexpr EGLint EGL_GREEN_SIZE_VALUE = 0x3023;
    constexpr EGLint EGL_BLUE_SIZE_VALUE = 0x3022;
    constexpr EGLint EGL_ALPHA_SIZE_VALUE = 0x3021;
    constexpr EGLint EGL_NATIVE_VISUAL_ID_VALUE = 0x302E;
    constexpr EGLint EGL_CONFIG_ID_VALUE = 0x3028;
    constexpr EGLint EGL_WIDTH_VALUE = 0x3057;
    constexpr EGLint EGL_HEIGHT_VALUE = 0x3056;
    constexpr EGLint EGL_OPENGL_ES_API_VALUE = 0x30A0;
    constexpr EGLint EGL_CONTEXT_CLIENT_VERSION_VALUE = 0x3098;
    constexpr EGLint EGL_D3D_TEXTURE_2D_SHARE_HANDLE_ANGLE_VALUE = 0x3200;
    constexpr EGLint GL_COLOR_BUFFER_BIT_VALUE = 0x00004000;

    using EGLConfig = void*;
    using EGLSurface = void*;
    using EGLContext = void*;
    using EglChooseConfig = EGLBoolean (WINAPI*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
    using EglGetConfigAttrib = EGLBoolean (WINAPI*)(EGLDisplay, EGLConfig, EGLint, EGLint*);
    using EglBindApi = EGLBoolean (WINAPI*)(EGLint);
    using EglCreateContext = EGLContext (WINAPI*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
    using EglCreatePbufferSurface = EGLSurface (WINAPI*)(EGLDisplay, EGLConfig, const EGLint*);
    using EglCreatePbufferFromClientBuffer = EGLSurface (WINAPI*)(EGLDisplay, EGLint, void*, EGLConfig, const EGLint*);
    using EglQuerySurfacePointer = EGLBoolean (WINAPI*)(EGLDisplay, EGLSurface, EGLint, void**);
    using EglMakeCurrent = EGLBoolean (WINAPI*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    using EglDestroySurface = EGLBoolean (WINAPI*)(EGLDisplay, EGLSurface);
    using EglDestroyContext = EGLBoolean (WINAPI*)(EGLDisplay, EGLContext);
    using EglGetError = EGLint (WINAPI*)();
    using GlViewport = void (WINAPI*)(int, int, int, int);
    using GlClearColor = void (WINAPI*)(float, float, float, float);
    using GlClear = void (WINAPI*)(unsigned int);
    using GlFinish = void (WINAPI*)();
    using GlFlush = void (WINAPI*)();
    using GlReadPixels = void (WINAPI*)(int, int, int, int, unsigned int, unsigned int, void*);
    using GlBindFramebuffer = void (WINAPI*)(unsigned int, unsigned int);
    using GlCheckFramebufferStatus = unsigned int (WINAPI*)(unsigned int);
    using GlGetError = unsigned int (WINAPI*)();

    auto egl = [&](const char* name) { return getProcAddress(name); };
    auto chooseConfig = reinterpret_cast<EglChooseConfig>(egl("eglChooseConfig"));
    auto getConfigAttrib = reinterpret_cast<EglGetConfigAttrib>(egl("eglGetConfigAttrib"));
    auto bindApi = reinterpret_cast<EglBindApi>(egl("eglBindAPI"));
    auto createContext = reinterpret_cast<EglCreateContext>(egl("eglCreateContext"));
    auto createPlainPbuffer = reinterpret_cast<EglCreatePbufferSurface>(egl("eglCreatePbufferSurface"));
    auto createPbuffer = reinterpret_cast<EglCreatePbufferFromClientBuffer>(egl("eglCreatePbufferFromClientBuffer"));
    auto querySurfacePointer = reinterpret_cast<EglQuerySurfacePointer>(egl("eglQuerySurfacePointerANGLE"));
    auto makeCurrent = reinterpret_cast<EglMakeCurrent>(egl("eglMakeCurrent"));
    auto destroySurface = reinterpret_cast<EglDestroySurface>(egl("eglDestroySurface"));
    auto destroyContext = reinterpret_cast<EglDestroyContext>(egl("eglDestroyContext"));
    auto getError = reinterpret_cast<EglGetError>(egl("eglGetError"));
    auto glViewport = reinterpret_cast<GlViewport>(egl("glViewport"));
    auto glClearColor = reinterpret_cast<GlClearColor>(egl("glClearColor"));
    auto glClear = reinterpret_cast<GlClear>(egl("glClear"));
    auto glFinish = reinterpret_cast<GlFinish>(egl("glFinish"));
    auto glFlush = reinterpret_cast<GlFlush>(egl("glFlush"));
    auto glReadPixels = reinterpret_cast<GlReadPixels>(egl("glReadPixels"));
    auto glBindFramebuffer = reinterpret_cast<GlBindFramebuffer>(egl("glBindFramebuffer"));
    auto glCheckFramebufferStatus = reinterpret_cast<GlCheckFramebufferStatus>(egl("glCheckFramebufferStatus"));
    auto glGetError = reinterpret_cast<GlGetError>(egl("glGetError"));

    if (!chooseConfig || !getConfigAttrib || !bindApi || !createContext || !createPlainPbuffer || !createPbuffer || !querySurfacePointer || !makeCurrent
        || !destroySurface || !destroyContext || !getError
        || !glViewport || !glClearColor || !glClear || !glFinish || !glFlush || !glReadPixels
        || !glBindFramebuffer || !glCheckFramebufferStatus || !glGetError) {
        std::wcerr << L"FAIL: ANGLE EGL/GL entry points required for D3D shared-handle import are unavailable.\n";
        return false;
    }

    ComPtr<ID3D11Device> angleDevice;
    angleDevice = angleD3DDevice; // AddRef the borrowed device returned by EGL_ANGLE_device_d3d11.
    if (!angleDevice) {
        std::wcerr << L"FAIL: ANGLE did not expose its D3D11 device.\n";
        return false;
    }

    ComPtr<IDXGIDevice> angleDxgiDevice;
    ComPtr<IDXGIAdapter> angleAdapter;
    DXGI_ADAPTER_DESC angleAdapterDescription{};
    if (FAILED(angleDevice.As(&angleDxgiDevice))
        || FAILED(angleDxgiDevice->GetAdapter(&angleAdapter))
        || FAILED(angleAdapter->GetDesc(&angleAdapterDescription))) {
        std::wcerr << L"FAIL: could not identify the ANGLE adapter for the shared texture.\n";
        return false;
    }

    LUID angleAdapterLuid = angleAdapterDescription.AdapterLuid;
    ComPtr<ID3D11Device> producerDevice;
    ComPtr<ID3D11DeviceContext> producerContext;
    LUID producerAdapterLuid{};
    if (!createDevice(D3D_DRIVER_TYPE_UNKNOWN, &angleAdapterLuid,
                      producerDevice, producerContext, producerAdapterLuid)) {
        std::wcerr << L"FAIL: could not create a second D3D11 device on the ANGLE adapter.\n";
        return false;
    }
    if (producerAdapterLuid.HighPart != angleAdapterLuid.HighPart
        || producerAdapterLuid.LowPart != angleAdapterLuid.LowPart) {
        std::wcerr << L"FAIL: shared texture producer and ANGLE selected different adapters.\n";
        return false;
    }

    D3D11_TEXTURE2D_DESC description{};
    description.Width = kWidth;
    description.Height = kHeight;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    description.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = producerDevice->CreateTexture2D(&description, nullptr, &texture);
    if (FAILED(hr)) {
        printHr(L"Create cross-device ANGLE shared D3D11 texture", hr);
        return false;
    }

    ComPtr<IDXGIKeyedMutex> producerMutex;
    hr = texture.As(&producerMutex);
    if (FAILED(hr) || !producerMutex) {
        printHr(L"Query producer IDXGIKeyedMutex", FAILED(hr) ? hr : E_NOINTERFACE);
        return false;
    }
    hr = producerMutex->AcquireSync(0, 10000);
    if (hr != S_OK) {
        printHr(L"Producer AcquireSync key 0 before EGL import", hr);
        return false;
    }
    hr = producerMutex->ReleaseSync(1);
    if (hr != S_OK) {
        printHr(L"Producer ReleaseSync key 1 before EGL import", hr);
        return false;
    }

    ComPtr<IDXGIResource> dxgiResource;
    hr = texture.As(&dxgiResource);
    if (FAILED(hr)) {
        printHr(L"Query IDXGIResource for ANGLE shared texture", hr);
        return false;
    }

    // EGL_ANGLE_d3d_share_handle_client_buffer maps to OpenSharedResource
    // in this pinned ANGLE build. Use its legacy DXGI share-handle contract.
    HANDLE sharedHandle = nullptr;
    hr = dxgiResource->GetSharedHandle(&sharedHandle);
    if (FAILED(hr) || !sharedHandle) {
        printHr(L"Get legacy ANGLE-importable DXGI shared handle", hr);
        return false;
    }

    const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE_VALUE, EGL_PBUFFER_BIT_VALUE,
        EGL_RENDERABLE_TYPE_VALUE, EGL_OPENGL_ES2_BIT_VALUE,
        EGL_RED_SIZE_VALUE, 8,
        EGL_GREEN_SIZE_VALUE, 8,
        EGL_BLUE_SIZE_VALUE, 8,
        EGL_ALPHA_SIZE_VALUE, 8,
        EGL_NONE_VALUE
    };
    std::vector<EGLConfig> configs(64);
    EGLint configCount = 0;
    if (!chooseConfig(display, configAttributes, configs.data(),
                      static_cast<EGLint>(configs.size()), &configCount) || configCount < 1) {
        std::wcerr << L"FAIL: eglChooseConfig for an ES2 pbuffer failed; EGL error=0x"
                   << std::hex << getError() << std::dec << L"\n";
                return false;
    }

    // EGL_NATIVE_VISUAL_ID is an EGL-native visual identifier, not guaranteed
    // to be the numeric DXGI_FORMAT value on ANGLE's D3D11 backend. Probe each
    // otherwise-compatible ES2 pbuffer config by attempting the actual shared
    // handle import; this is the authoritative compatibility test.
    // The EGL_TEXTURE_* attributes belong to the client-buffer surface, not to
    // eglChooseConfig. EGL_BIND_TO_TEXTURE_RGBA is for eglBindTexImage on an
    // ordinary pbuffer and over-constrains ANGLE's D3D11 config set here.
    // The shared-handle extension surface still receives the explicit
    // EGL_TEXTURE_TARGET / EGL_TEXTURE_FORMAT pair below.
    const EGLint surfaceAttributes[] = {
        EGL_WIDTH_VALUE, static_cast<EGLint>(kWidth),
        EGL_HEIGHT_VALUE, static_cast<EGLint>(kHeight),
        EGL_TEXTURE_TARGET_VALUE, EGL_TEXTURE_2D_VALUE,
        EGL_TEXTURE_FORMAT_VALUE, EGL_TEXTURE_RGBA_VALUE,
        EGL_NONE_VALUE
    };
    EGLConfig config = nullptr;
    EGLSurface surface = nullptr;
    for (EGLint index = 0; index < configCount; ++index) {
        const auto candidate = configs[static_cast<size_t>(index)];
        EGLint nativeVisualId = 0;
        EGLint configId = 0;
        EGLint red = 0, green = 0, blue = 0, alpha = 0, surfaceType = 0, renderableType = 0, bindToTextureRgba = 0;
        getConfigAttrib(display, candidate, EGL_NATIVE_VISUAL_ID_VALUE, &nativeVisualId);
        getConfigAttrib(display, candidate, EGL_CONFIG_ID_VALUE, &configId);
        getConfigAttrib(display, candidate, EGL_RED_SIZE_VALUE, &red);
        getConfigAttrib(display, candidate, EGL_GREEN_SIZE_VALUE, &green);
        getConfigAttrib(display, candidate, EGL_BLUE_SIZE_VALUE, &blue);
        getConfigAttrib(display, candidate, EGL_ALPHA_SIZE_VALUE, &alpha);
        getConfigAttrib(display, candidate, EGL_SURFACE_TYPE_VALUE, &surfaceType);
        getConfigAttrib(display, candidate, EGL_RENDERABLE_TYPE_VALUE, &renderableType);
        getConfigAttrib(display, candidate, EGL_BIND_TO_TEXTURE_RGBA_VALUE, &bindToTextureRgba);
        std::wcout << L"EGL candidate " << configId << L": RGBA=" << red << L"/" << green
                   << L"/" << blue << L"/" << alpha << L", surfaceType=0x" << std::hex
                   << surfaceType << L", renderableType=0x" << renderableType << std::dec
                   << L", bindToTextureRGBA=" << bindToTextureRgba << L".\n";
        auto candidateSurface = createPbuffer(display, EGL_D3D_TEXTURE_2D_SHARE_HANDLE_ANGLE_VALUE,
                                              sharedHandle, candidate, surfaceAttributes);
        if (candidateSurface) {
            config = candidate;
            surface = candidateSurface;
            std::wcout << L"ANGLE legacy shared-handle import selected EGL config " << configId
                       << L" (native visual id " << nativeVisualId << L").\n";
            break;
        }
        const auto error = getError();
        std::wcout << L"ANGLE legacy shared-handle import rejected EGL config " << configId
                   << L" (native visual id " << nativeVisualId << L", EGL error=0x"
                   << std::hex << error << std::dec << L").\n";
    }
    if (!config || !surface) {
        std::wcerr << L"FAIL: no ES2 pbuffer config accepted the BGRA8 keyed-mutex legacy DXGI shared D3D11 handle.\n";
        return false;
    }

    constexpr EGLint EGL_DXGI_KEYED_MUTEX_ANGLE_VALUE = 0x33A2;
    void* rawAngleMutex = nullptr;
    if (!querySurfacePointer(display, surface, EGL_DXGI_KEYED_MUTEX_ANGLE_VALUE, &rawAngleMutex) || !rawAngleMutex) {
        std::wcerr << L"FAIL: eglQuerySurfacePointerANGLE(EGL_DXGI_KEYED_MUTEX_ANGLE) returned no mutex; EGL error=0x"
                   << std::hex << getError() << std::dec << L"\n";
        destroySurface(display, surface);
        return false;
    }
    auto* angleMutex = static_cast<IDXGIKeyedMutex*>(rawAngleMutex);

    if (!bindApi(EGL_OPENGL_ES_API_VALUE)) {
        std::wcerr << L"FAIL: eglBindAPI(OpenGL ES) failed; EGL error=0x"
                   << std::hex << getError() << std::dec << L"\n";
        destroySurface(display, surface);
                return false;
    }

    const EGLint contextAttributes[] = { EGL_CONTEXT_CLIENT_VERSION_VALUE, 2, EGL_NONE_VALUE };
    EGLContext context = createContext(display, config, nullptr, contextAttributes);
    if (!context) {
        std::wcerr << L"FAIL: eglCreateContext failed; EGL error=0x"
                   << std::hex << getError() << std::dec << L"\n";
        destroySurface(display, surface);
                return false;
    }

    bool rendered = false;
    bool glPixelMatches = false;
    constexpr unsigned char expectedRgba[4] = { 32, 64, 191, 255 };
    const EGLint baselineAttributes[] = {
        EGL_WIDTH_VALUE, static_cast<EGLint>(kWidth),
        EGL_HEIGHT_VALUE, static_cast<EGLint>(kHeight),
        EGL_NONE_VALUE
    };
    EGLSurface baselineSurface = createPlainPbuffer(display, config, baselineAttributes);
    if (!baselineSurface) {
        std::wcerr << L"FAIL: eglCreatePbufferSurface baseline failed; EGL error=0x"
                   << std::hex << getError() << std::dec << L"\\n";
    } else if (makeCurrent(display, baselineSurface, baselineSurface, context)) {
        glBindFramebuffer(0x8D40 /* GL_FRAMEBUFFER */, 0);
        glViewport(0, 0, static_cast<int>(kWidth), static_cast<int>(kHeight));
        glClearColor(0.125f, 0.25f, 0.75f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT_VALUE);
        glFlush();
        glFinish();
        unsigned char baselinePixel[4]{};
        glReadPixels(static_cast<int>(kWidth / 2), static_cast<int>(kHeight / 2),
                     1, 1, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, baselinePixel);
        const auto baselineError = glGetError();
        const auto baselineFramebuffer = glCheckFramebufferStatus(0x8D40 /* GL_FRAMEBUFFER */);
        const bool baselineMatches = std::equal(std::begin(expectedRgba), std::end(expectedRgba), baselinePixel);
        std::wcout << L"Baseline pbuffer: framebuffer=0x" << std::hex << baselineFramebuffer
                   << L", GL error=0x" << baselineError << std::dec << L", RGBA="
                   << static_cast<unsigned>(baselinePixel[0]) << L", "
                   << static_cast<unsigned>(baselinePixel[1]) << L", "
                   << static_cast<unsigned>(baselinePixel[2]) << L", "
                   << static_cast<unsigned>(baselinePixel[3]) << L".\\n";
        makeCurrent(display, nullptr, nullptr, nullptr);
        if (!baselineMatches || baselineError != 0 || baselineFramebuffer != 0x8CD5 /* GL_FRAMEBUFFER_COMPLETE */) {
            std::wcerr << L"FAIL: normal EGL pbuffer did not render the expected GLES clear.\\n";
        } else if (makeCurrent(display, surface, surface, context)) {
            const HRESULT acquireResult = angleMutex->AcquireSync(1, 10000);
            if (acquireResult != S_OK) {
                printHr(L"ANGLE EGL surface AcquireSync key 1", acquireResult);
            } else {
            glBindFramebuffer(0x8D40 /* GL_FRAMEBUFFER */, 0);
            glViewport(0, 0, static_cast<int>(kWidth), static_cast<int>(kHeight));
            glClearColor(0.125f, 0.25f, 0.75f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT_VALUE);
            glFlush();
            glFinish();
            unsigned char glPixel[4]{};
            glReadPixels(static_cast<int>(kWidth / 2), static_cast<int>(kHeight / 2),
                         1, 1, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, glPixel);
            const auto glError = glGetError();
            const auto framebufferStatus = glCheckFramebufferStatus(0x8D40 /* GL_FRAMEBUFFER */);
            glPixelMatches = std::equal(std::begin(expectedRgba), std::end(expectedRgba), glPixel);
            std::wcout << L"Imported pbuffer: framebuffer=0x" << std::hex << framebufferStatus
                       << L", GL error=0x" << glError << std::dec << L", RGBA="
                       << static_cast<unsigned>(glPixel[0]) << L", "
                       << static_cast<unsigned>(glPixel[1]) << L", "
                       << static_cast<unsigned>(glPixel[2]) << L", "
                       << static_cast<unsigned>(glPixel[3])
                       << (glPixelMatches ? L" (PASS)" : L" (MISMATCH)") << L"\\n";
            glFlush();
            glFinish();
            const HRESULT releaseResult = angleMutex->ReleaseSync(2);
            if (releaseResult != S_OK) {
                printHr(L"ANGLE EGL surface ReleaseSync key 2", releaseResult);
            } else if (glPixelMatches && glError == 0
                && framebufferStatus == 0x8CD5 /* GL_FRAMEBUFFER_COMPLETE */) {
                rendered = true;
            } else {
                std::wcerr << L"FAIL: imported pbuffer render/flush failed; EGL error=0x"
                           << std::hex << getError() << std::dec << L"\\n";
            }
            }
        } else {
            std::wcerr << L"FAIL: eglMakeCurrent for imported shared-handle pbuffer failed; EGL error=0x"
                       << std::hex << getError() << std::dec << L"\\n";
        }
    } else {
        std::wcerr << L"FAIL: eglMakeCurrent for baseline pbuffer failed; EGL error=0x"
                   << std::hex << getError() << std::dec << L"\\n";
    }
    if (baselineSurface)
        destroySurface(display, baselineSurface);
    if (!makeCurrent(display, nullptr, nullptr, nullptr))
        rendered = false;

    if (!makeCurrent(display, nullptr, nullptr, nullptr)) {
        std::wcerr << L"FAIL: eglMakeCurrent(unbind) failed; EGL error=0x"
                   << std::hex << getError() << std::dec << L"\n";
        rendered = false;
    }
    destroySurface(display, surface);
    destroyContext(display, context);

    if (!rendered) {
                return false;
    }

    hr = producerMutex->AcquireSync(2, 10000);
    if (hr != S_OK) {
        printHr(L"Producer AcquireSync key 2 before ANGLE readback", hr);
        return false;
    }

    D3D11_TEXTURE2D_DESC stagingDescription = description;
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDescription.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    hr = producerDevice->CreateTexture2D(&stagingDescription, nullptr, &staging);
    if (FAILED(hr)) {
        printHr(L"Create ANGLE render verification staging texture", hr);
        producerMutex->ReleaseSync(3);
        return false;
    }

    producerContext->CopyResource(staging.Get(), texture.Get());
    producerContext->Flush();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = producerContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        printHr(L"Map ANGLE-rendered shared D3D texture", hr);
        producerMutex->ReleaseSync(3);
        return false;
    }

    constexpr unsigned char expected[4] = { 191, 64, 32, 255 };
    const auto* pixel = static_cast<const unsigned char*>(mapped.pData);
    const bool matches = std::equal(std::begin(expected), std::end(expected), pixel);
    std::wcout << L"Producer D3D11 center pixel BGRA: "
               << static_cast<unsigned>(pixel[(kHeight / 2) * mapped.RowPitch + (kWidth / 2) * 4 + 0]) << L", "
               << static_cast<unsigned>(pixel[(kHeight / 2) * mapped.RowPitch + (kWidth / 2) * 4 + 1]) << L", "
               << static_cast<unsigned>(pixel[(kHeight / 2) * mapped.RowPitch + (kWidth / 2) * 4 + 2]) << L", "
               << static_cast<unsigned>(pixel[(kHeight / 2) * mapped.RowPitch + (kWidth / 2) * 4 + 3]) << L"\n";
    producerContext->Unmap(staging.Get(), 0);
    hr = producerMutex->ReleaseSync(3);
    if (hr != S_OK) {
        printHr(L"Producer ReleaseSync key 3 after ANGLE readback", hr);
        return false;
    }
    if (!glPixelMatches) {
        std::wcerr << L"FAIL: GLES readback did not contain the requested clear color.\n";
        return false;
    }
    if (!matches) {
        std::wcerr << L"FAIL: D3D11 producer did not observe the ANGLE-rendered BGRA8 pixel pattern.\n";
        return false;
    }

    if (!presentWithDirectComposition(producerDevice.Get(), producerContext.Get(), texture.Get()))
        return false;
    std::wcout << L"PASS: ANGLE imported a legacy keyed-mutex DXGI shared D3D11 texture, acquired/released the EGL surface mutex around GLES rendering, and the producer device verified BGRA pixels after the key-2 handoff.\n";
    return true;
}

bool probeAngleRuntime()
{
    HMODULE module = LoadLibraryW(L"libEGL.dll");
    if (!module) {
        std::wcout << L"ANGLE EGL probe: FAIL (libEGL.dll is missing from the staged WinCairo runtime).\n";
        return false;
    }

    auto getProcAddress = reinterpret_cast<EglGetProcAddress>(GetProcAddress(module, "eglGetProcAddress"));
    auto initialize = reinterpret_cast<EglInitialize>(GetProcAddress(module, "eglInitialize"));
    auto queryString = reinterpret_cast<EglQueryString>(GetProcAddress(module, "eglQueryString"));
    auto terminate = reinterpret_cast<EglTerminate>(GetProcAddress(module, "eglTerminate"));
    auto getDisplay = reinterpret_cast<EglGetDisplay>(GetProcAddress(module, "eglGetDisplay"));
    if (!getProcAddress || !initialize || !queryString || !terminate) {
        std::wcout << L"ANGLE EGL probe: SKIP (required EGL entry points are unavailable).\n";
        FreeLibrary(module);
        return false;
    }

    auto getPlatformDisplay = reinterpret_cast<EglGetPlatformDisplay>(
        getProcAddress("eglGetPlatformDisplayEXT"));

    EGLDisplay display = nullptr;
    if (getPlatformDisplay) {
        const EGLint attributes[] = {
            EGL_PLATFORM_ANGLE_TYPE_VALUE, EGL_PLATFORM_ANGLE_TYPE_D3D11_VALUE,
            EGL_PLATFORM_ANGLE_DEVICE_TYPE_VALUE, EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_VALUE,
            EGL_NONE_VALUE
        };
        display = getPlatformDisplay(EGL_PLATFORM_ANGLE_VALUE, nullptr, attributes);
    }
    if (!display && getDisplay)
        display = getDisplay(nullptr);

    EGLint major = 0;
    EGLint minor = 0;
    if (!display || !initialize(display, &major, &minor)) {
        std::wcout << L"ANGLE EGL probe: SKIP (ANGLE could not initialize a D3D11 display on this runner).\n";
        FreeLibrary(module);
        return false;
    }

    const char* vendor = queryString(display, EGL_VENDOR_VALUE);
    const char* version = queryString(display, EGL_VERSION_VALUE);
    const char* extensions = queryString(display, EGL_EXTENSIONS_VALUE);
    std::wcout << L"ANGLE EGL vendor: " << (vendor ? vendor : "(unknown)") << L"\n";
    std::wcout << L"ANGLE EGL version: " << (version ? version : "(unknown)") << L"\n";
    std::wcout << L"ANGLE EGL display extensions:\n" << (extensions ? extensions : "(unavailable)") << L"\n";

    const char* candidates[] = {
        "EGL_ANGLE_d3d_texture_client_buffer",
        "EGL_ANGLE_d3d_share_handle_client_buffer",
        "EGL_ANGLE_surface_d3d_texture_2d_share_handle",
        "EGL_ANGLE_device_d3d",
        "EGL_ANGLE_keyed_mutex",
        "EGL_EXT_device_query"
    };
    for (const char* candidate : candidates)
        std::wcout << L"  " << candidate << L": "
                   << (hasExtension(extensions, candidate) ? L"present" : L"absent") << L"\n";

    auto queryDisplayAttrib = reinterpret_cast<EglQueryDisplayAttrib>(
        getProcAddress("eglQueryDisplayAttribEXT"));
    auto queryDeviceAttrib = reinterpret_cast<EglQueryDeviceAttrib>(
        getProcAddress("eglQueryDeviceAttribEXT"));
    auto queryDeviceString = reinterpret_cast<EglQueryDeviceString>(
        getProcAddress("eglQueryDeviceStringEXT"));

    bool angleRenderPassed = false;
    EGLAttrib deviceValue = 0;
    if (queryDisplayAttrib && queryDeviceAttrib && queryDeviceString
        && queryDisplayAttrib(display, EGL_DEVICE_EXT_VALUE, &deviceValue)
        && deviceValue) {
        auto device = reinterpret_cast<EGLDeviceEXT>(deviceValue);
        const char* deviceExtensions = queryDeviceString(device, EGL_EXTENSIONS_VALUE);
        std::wcout << L"ANGLE EGL device extensions:\n"
                   << (deviceExtensions ? deviceExtensions : "(unavailable)") << L"\n";
        const bool hasD3D11Device = hasExtension(deviceExtensions, "EGL_ANGLE_device_d3d11")
            || hasExtension(deviceExtensions, "EGL_ANGLE_device_d3d");
        std::wcout << L"  EGL_ANGLE_device_d3d11: "
                   << (hasD3D11Device ? L"present" : L"absent") << L"\n";

        EGLAttrib d3dDeviceValue = 0;
        if (hasD3D11Device && queryDeviceAttrib(device, EGL_D3D11_DEVICE_ANGLE_VALUE, &d3dDeviceValue)
            && d3dDeviceValue) {
            auto* d3dDevice = reinterpret_cast<ID3D11Device*>(d3dDeviceValue);
            ComPtr<IDXGIDevice> dxgiDevice;
            ComPtr<IDXGIAdapter> adapter;
            DXGI_ADAPTER_DESC adapterDescription{};
            if (SUCCEEDED(d3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice)))
                && SUCCEEDED(dxgiDevice->GetAdapter(&adapter))
                && SUCCEEDED(adapter->GetDesc(&adapterDescription))) {
                std::wcout << L"ANGLE D3D11 adapter LUID: "
                    << std::hex << static_cast<unsigned long>(adapterDescription.AdapterLuid.HighPart)
                    << L":" << adapterDescription.AdapterLuid.LowPart << std::dec << L"\n";
            }
            angleRenderPassed = renderThroughAngleD3DTexture(display, getProcAddress, d3dDevice);
        }
    } else {
        std::wcerr << L"FAIL: ANGLE EGL device query did not expose its D3D11 device.\n";
    }

    terminate(display);
    FreeLibrary(module);
    return angleRenderPassed;
}
}

int wmain(int argc, wchar_t** argv)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (argc >= 6 && std::wstring(argv[1]) == L"--consume") {
        const auto pipeHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(_wcstoui64(argv[2], nullptr, 10)));
        char handleText[64]{};
        DWORD bytesRead = 0;
        if (!ReadFile(pipeHandle, handleText, sizeof(handleText) - 1, &bytesRead, nullptr) || !bytesRead) {
            std::wcerr << L"FAIL: child could not receive its legacy DXGI shared-handle token. Error=" << GetLastError() << L"\n";
            CloseHandle(pipeHandle);
            return 9;
        }
        CloseHandle(pipeHandle);
        handleText[bytesRead] = '\0';
        const auto rawHandle = static_cast<uintptr_t>(_strtoui64(handleText, nullptr, 10));
        LUID luid{};
        luid.HighPart = static_cast<LONG>(_wcstoui64(argv[3], nullptr, 10));
        luid.LowPart = static_cast<DWORD>(_wcstoui64(argv[4], nullptr, 10));
        const auto driverType = static_cast<D3D_DRIVER_TYPE>(_wtoi(argv[5]));
        return consumeSharedTexture(reinterpret_cast<HANDLE>(rawHandle), driverType, luid);
    }

    std::wcout << L"Blaze WinCairo shared-D3D capability probe\n";
    if (!probeAngleRuntime())
        return 2;
    if (!runCrossProcessSharedTextureProbe())
        return 1;

    std::wcout << L"Probe result: D3D11 cross-process shared-texture transport is available on this machine.\n";
    std::wcout << L"Note: this does not yet prove that the pinned ANGLE build can import the shared texture or that WebKit WCScene can render into it.\n";
    return 0;
}
