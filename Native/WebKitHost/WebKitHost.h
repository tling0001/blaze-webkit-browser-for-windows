#pragma once
#include <Windows.h>

#ifdef WEBKITHOST_EXPORTS
#define WB_API extern "C" __declspec(dllexport)
#else
#define WB_API extern "C" __declspec(dllimport)
#endif

struct WB_VIEW;
using WB_VIEW_HANDLE = WB_VIEW*;

WB_API WB_VIEW_HANDLE WB_Create(HWND parent, int x, int y, int width, int height, const wchar_t* initialUrl);
WB_API void WB_Destroy(WB_VIEW_HANDLE view);
WB_API void WB_SetBounds(WB_VIEW_HANDLE view, int x, int y, int width, int height);
WB_API void WB_Navigate(WB_VIEW_HANDLE view, const wchar_t* url);
WB_API void WB_GoBack(WB_VIEW_HANDLE view);
WB_API void WB_GoForward(WB_VIEW_HANDLE view);
WB_API void WB_Reload(WB_VIEW_HANDLE view);
WB_API void WB_Stop(WB_VIEW_HANDLE view);
WB_API BOOL WB_CanGoBack(WB_VIEW_HANDLE view);
WB_API BOOL WB_CanGoForward(WB_VIEW_HANDLE view);
WB_API int WB_GetCurrentUrl(WB_VIEW_HANDLE view, wchar_t* buffer, int capacity);
WB_API int WB_GetTitle(WB_VIEW_HANDLE view, wchar_t* buffer, int capacity);
WB_API void WB_SetJavaScriptEnabled(WB_VIEW_HANDLE view, BOOL enabled);
WB_API void WB_SetLoadsImagesAutomatically(WB_VIEW_HANDLE view, BOOL enabled);
WB_API void WB_ClearWebsiteData(WB_VIEW_HANDLE view);
