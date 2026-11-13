#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
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

    ComPtr<ID3D11Device1> device1;
    HRESULT hr = device.As(&device1);
    if (FAILED(hr)) {
        printHr(L"Query ID3D11Device1", hr);
        return 12;
    }

    ComPtr<ID3D11Texture2D> sharedTexture;
    hr = device1->OpenSharedResource1(sharedHandle, IID_PPV_ARGS(&sharedTexture));
    if (FAILED(hr)) {
        printHr(L"ID3D11Device1::OpenSharedResource1", hr);
        return 13;
    }

    ComPtr<IDXGIKeyedMutex> mutex;
    hr = sharedTexture.As(&mutex);
    if (FAILED(hr)) {
        printHr(L"Query IDXGIKeyedMutex", hr);
        return 14;
    }

    hr = mutex->AcquireSync(1, 10000);
    if (FAILED(hr)) {
        printHr(L"Child keyed-mutex acquire", hr);
        return 15;
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
        mutex->ReleaseSync(2);
        printHr(L"Create staging texture", hr);
        return 16;
    }

    context->CopyResource(staging.Get(), sharedTexture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        mutex->ReleaseSync(2);
        printHr(L"Map staging texture", hr);
        return 17;
    }

    const auto* pixel = static_cast<const unsigned char*>(mapped.pData);
    const bool matches = std::equal(std::begin(kExpectedPixel), std::end(kExpectedPixel), pixel);
    context->Unmap(staging.Get(), 0);
    const HRESULT releaseResult = mutex->ReleaseSync(2);
    if (FAILED(releaseResult)) {
        printHr(L"Child keyed-mutex release", releaseResult);
        return 18;
    }

    if (!matches) {
        std::wcerr << L"FAIL: shared texture pixel did not match the known BGRA test pattern.\n";
        return 19;
    }

    std::wcout << L"PASS: child process opened the NT shared texture, synchronized with the keyed mutex, and verified the GPU resource contents.\n";
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
    description.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = device->CreateTexture2D(&description, nullptr, &texture);
    if (FAILED(hr)) {
        printHr(L"Create NT-handle/keyed-mutex texture", hr);
        return false;
    }

    ComPtr<IDXGIResource1> resource;
    hr = texture.As(&resource);
    if (FAILED(hr)) {
        printHr(L"Query IDXGIResource1", hr);
        return false;
    }

    HANDLE sharedHandle = nullptr;
    hr = resource->CreateSharedHandle(
        nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &sharedHandle);
    if (FAILED(hr) || !sharedHandle) {
        printHr(L"IDXGIResource1::CreateSharedHandle", hr);
        return false;
    }

    ComPtr<IDXGIKeyedMutex> mutex;
    hr = texture.As(&mutex);
    if (FAILED(hr)) {
        CloseHandle(sharedHandle);
        printHr(L"Query parent IDXGIKeyedMutex", hr);
        return false;
    }

    hr = mutex->AcquireSync(0, 5000);
    if (FAILED(hr)) {
        CloseHandle(sharedHandle);
        printHr(L"Parent keyed-mutex acquire", hr);
        return false;
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(kWidth) * kHeight * 4);
    for (size_t offset = 0; offset < pixels.size(); offset += 4)
        std::copy(std::begin(kExpectedPixel), std::end(kExpectedPixel), pixels.begin() + offset);
    context->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), kWidth * 4, 0);
    hr = mutex->ReleaseSync(1);
    if (FAILED(hr)) {
        CloseHandle(sharedHandle);
        printHr(L"Parent keyed-mutex release", hr);
        return false;
    }

    std::wstring executable(MAX_PATH, L'\0');
    const DWORD executableLength = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!executableLength || executableLength >= executable.size()) {
        CloseHandle(sharedHandle);
        std::wcerr << L"FAIL: unable to resolve the probe executable path.\n";
        return false;
    }
    executable.resize(executableLength);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + executable + L"\" --consume ";
    // Create the child suspended so the duplicated handle is valid before it starts.
    commandLine += L"0 " + std::to_wstring(adapterLuid.HighPart) + L" "
        + std::to_wstring(adapterLuid.LowPart) + L" " + std::to_wstring(static_cast<int>(driverType));
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    if (!CreateProcessW(executable.c_str(), mutableCommandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, nullptr, &startup, &process)) {
        CloseHandle(sharedHandle);
        std::wcerr << L"FAIL: CreateProcessW error=" << GetLastError() << L"\n";
        return false;
    }

    HANDLE remoteHandle = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), sharedHandle, process.hProcess, &remoteHandle,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) {
        std::wcerr << L"FAIL: DuplicateHandle error=" << GetLastError() << L"\n";
        TerminateProcess(process.hProcess, 20);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(sharedHandle);
        return false;
    }

    // Replace the placeholder with the target process's handle value.
    commandLine = L"\"" + executable + L"\" --consume "
        + std::to_wstring(reinterpret_cast<uintptr_t>(remoteHandle)) + L" "
        + std::to_wstring(adapterLuid.HighPart) + L" "
        + std::to_wstring(adapterLuid.LowPart) + L" "
        + std::to_wstring(static_cast<int>(driverType));
    mutableCommandLine.assign(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');
    // The process was created suspended; update its command line before execution is not
    // supported by Win32. Use a startup environment variable instead would add complexity,
    // so this probe uses a small inherited command-line bootstrap below.
    // Terminate and relaunch now that the remote handle value is known.
    TerminateProcess(process.hProcess, 0);
    WaitForSingleObject(process.hProcess, 5000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    if (!CreateProcessW(executable.c_str(), mutableCommandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, nullptr, &startup, &process)) {
        CloseHandle(sharedHandle);
        std::wcerr << L"FAIL: CreateProcessW (handle bootstrap) error=" << GetLastError() << L"\n";
        return false;
    }

    // Duplicate the original shared handle into the second child process.
    HANDLE secondRemoteHandle = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), sharedHandle, process.hProcess, &secondRemoteHandle,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) {
        std::wcerr << L"FAIL: DuplicateHandle (child) error=" << GetLastError() << L"\n";
        TerminateProcess(process.hProcess, 21);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(sharedHandle);
        return false;
    }

    // Recreate with the exact duplicated handle value. This two-pass bootstrap is
    // intentionally replaced by a named inherited event/handle protocol in the next probe.
    // The current probe validates OS sharing only when the remote handle value is stable.
    if (reinterpret_cast<uintptr_t>(secondRemoteHandle) != reinterpret_cast<uintptr_t>(remoteHandle)) {
        // Different processes can receive different handle values. Pass the actual value
        // by terminating and relaunching once more; the final child handle is duplicated
        // after process creation, so a fixed command line cannot carry it. This path is
        // handled by the environment block in the final implementation.
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(sharedHandle);
        std::wcerr << L"FAIL: child handle bootstrap requires an explicit IPC channel.\n";
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
    CloseHandle(sharedHandle);
    if (waitResult != WAIT_OBJECT_0 || childExitCode != 0) {
        std::wcerr << L"FAIL: child shared-texture probe exit code=" << childExitCode << L"\n";
        return false;
    }

    std::wcout << L"PASS: cross-process D3D11 NT-handle sharing and keyed-mutex synchronization.\n";
    return true;
}

using EGLDisplay = void*;
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

void probeAngleRuntime()
{
    HMODULE module = LoadLibraryW(L"libEGL.dll");
    if (!module) {
        std::wcout << L"ANGLE EGL probe: SKIP (libEGL.dll is not present beside the staged WinCairo runtime).\n";
        return;
    }

    auto getProcAddress = reinterpret_cast<EglGetProcAddress>(GetProcAddress(module, "eglGetProcAddress"));
    auto initialize = reinterpret_cast<EglInitialize>(GetProcAddress(module, "eglInitialize"));
    auto queryString = reinterpret_cast<EglQueryString>(GetProcAddress(module, "eglQueryString"));
    auto terminate = reinterpret_cast<EglTerminate>(GetProcAddress(module, "eglTerminate"));
    auto getDisplay = reinterpret_cast<EglGetDisplay>(GetProcAddress(module, "eglGetDisplay"));
    if (!getProcAddress || !initialize || !queryString || !terminate) {
        std::wcout << L"ANGLE EGL probe: SKIP (required EGL entry points are unavailable).\n";
        FreeLibrary(module);
        return;
    }

    auto getPlatformDisplay = reinterpret_cast<EglGetPlatformDisplay>(
        getProcAddress("eglGetPlatformDisplayEXT"));
    if (!getPlatformDisplay)
        getPlatformDisplay = reinterpret_cast<EglGetPlatformDisplay>(GetProcAddress(module, "eglGetPlatformDisplay"));

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
        return;
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
        }
    } else {
        std::wcout << L"ANGLE EGL device query: unavailable.\n";
    }

    terminate(display);
    FreeLibrary(module);
}
}

int wmain(int argc, wchar_t** argv)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (argc >= 6 && std::wstring(argv[1]) == L"--consume") {
        const auto rawHandle = static_cast<uintptr_t>(_wcstoui64(argv[2], nullptr, 10));
        LUID luid{};
        luid.HighPart = static_cast<LONG>(_wcstoui64(argv[3], nullptr, 10));
        luid.LowPart = static_cast<DWORD>(_wcstoui64(argv[4], nullptr, 10));
        const auto driverType = static_cast<D3D_DRIVER_TYPE>(_wtoi(argv[5]));
        return consumeSharedTexture(reinterpret_cast<HANDLE>(rawHandle), driverType, luid);
    }

    std::wcout << L"Blaze WinCairo shared-D3D capability probe\n";
    probeAngleRuntime();
    if (!runCrossProcessSharedTextureProbe())
        return 1;

    std::wcout << L"Probe result: D3D11 cross-process shared-texture transport is available on this machine.\n";
    std::wcout << L"Note: this does not yet prove that the pinned ANGLE build can import the shared texture or that WebKit WCScene can render into it.\n";
    return 0;
}
