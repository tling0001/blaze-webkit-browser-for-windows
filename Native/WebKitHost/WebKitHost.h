#pragma once
#include <Windows.h>

#ifdef WEBKITHOST_EXPORTS
#define WB_API extern "C" __declspec(dllexport)
#else
#define WB_API extern "C" __declspec(dllimport)
#endif

#include "SharedSurfaceProtocol.h"

struct WB_VIEW;
using WB_VIEW_HANDLE = WB_VIEW*;
struct WB_GPU_SURFACE;
using WB_GPU_SURFACE_HANDLE = WB_GPU_SURFACE*;

using WB_DOWNLOAD_CALLBACK = void (__cdecl*)(const wchar_t* filePath, int status, long long bytesWritten, long long totalBytesExpected, void* userData);
using WB_PERMISSION_CALLBACK = int (__cdecl*)(const wchar_t* origin, int permissionType, void* userData);
using WB_JAVASCRIPT_DIALOG_CALLBACK = void (__cdecl*)(int dialogType, const wchar_t* origin, const wchar_t* message, const wchar_t* defaultValue, const void* listener, void* userData);
using WB_CREDENTIAL_BRIDGE_CALLBACK = void (__cdecl*)(WB_VIEW_HANDLE view, const wchar_t* origin, const wchar_t* payload, const void* listener, void* userData);
using WB_NEW_PAGE_CALLBACK = int (__cdecl*)(WB_VIEW_HANDLE view, void* userData);
using WB_CLOSE_PAGE_CALLBACK = void (__cdecl*)(WB_VIEW_HANDLE view, void* userData);

// Host-owned D3D11 shared-texture ring. The returned legacy shared-resource
// tokens remain valid only while this surface object is alive; never CloseHandle them.
WB_API WB_GPU_SURFACE_HANDLE WB_GpuSurfaceCreate(std::uint64_t viewIdentifier, std::uint32_t width, std::uint32_t height, std::uint64_t generation);
WB_API BOOL WB_GpuSurfaceGetDescriptor(WB_GPU_SURFACE_HANDLE surface, Blaze::Gpu::SharedSurfaceDescriptor* descriptor);
WB_API void WB_GpuSurfaceDestroy(WB_GPU_SURFACE_HANDLE surface);

WB_API WB_VIEW_HANDLE WB_Create(HWND parent, int x, int y, int width, int height, const wchar_t* initialUrl);
WB_API void WB_PrepareDestroy(WB_VIEW_HANDLE view);
WB_API void WB_Destroy(WB_VIEW_HANDLE view);
WB_API BOOL WB_SetBounds(WB_VIEW_HANDLE view, int x, int y, int width, int height);
WB_API HWND WB_GetWindow(WB_VIEW_HANDLE view);
WB_API HWND WB_GetHostWindow(WB_VIEW_HANDLE view);
WB_API void WB_Navigate(WB_VIEW_HANDLE view, const wchar_t* url);
WB_API void WB_SetUserAgent(WB_VIEW_HANDLE view, const wchar_t* userAgent);
WB_API void WB_GoBack(WB_VIEW_HANDLE view);
WB_API void WB_GoForward(WB_VIEW_HANDLE view);
WB_API void WB_Reload(WB_VIEW_HANDLE view);
WB_API void WB_Stop(WB_VIEW_HANDLE view);
WB_API BOOL WB_CanGoBack(WB_VIEW_HANDLE view);
WB_API BOOL WB_CanGoForward(WB_VIEW_HANDLE view);
WB_API double WB_GetEstimatedProgress(WB_VIEW_HANDLE view);
WB_API int WB_GetCurrentUrl(WB_VIEW_HANDLE view, wchar_t* buffer, int capacity);
WB_API int WB_GetTitle(WB_VIEW_HANDLE view, wchar_t* buffer, int capacity);
WB_API void WB_SetJavaScriptEnabled(WB_VIEW_HANDLE view, BOOL enabled);
WB_API void WB_SetLoadsImagesAutomatically(WB_VIEW_HANDLE view, BOOL enabled);
WB_API void WB_ClearWebsiteData(WB_VIEW_HANDLE view);
WB_API void WB_SetDownloadCallback(WB_VIEW_HANDLE view, WB_DOWNLOAD_CALLBACK callback, void* userData);
WB_API void WB_SetPermissionCallback(WB_VIEW_HANDLE view, WB_PERMISSION_CALLBACK callback, void* userData);
WB_API void WB_SetJavaScriptDialogCallback(WB_VIEW_HANDLE view, WB_JAVASCRIPT_DIALOG_CALLBACK callback, void* userData);
WB_API void WB_SetCredentialBridgeCallback(WB_VIEW_HANDLE view, WB_CREDENTIAL_BRIDGE_CALLBACK callback, void* userData);
WB_API void WB_EvaluateJavaScript(WB_VIEW_HANDLE view, const wchar_t* script);
WB_API void WB_SetPopupCallbacks(WB_VIEW_HANDLE view, WB_NEW_PAGE_CALLBACK newPageCallback, WB_CLOSE_PAGE_CALLBACK closePageCallback, void* userData);
// The WebKit result-listener references are const-qualified opaque pointers.
// Preserve that qualification across the native callback and managed bridge.
WB_API void WB_ResolveJavaScriptDialog(const void* listener, int dialogType, BOOL accepted, const wchar_t* response);
