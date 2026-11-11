#include "WebKitHost.h"

#include <WebKit/WebKit2_C.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKContext.h>
#include <WebKit/WKPreferencesRef.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKView.h>
#include <WebKit/WKWebsiteDataStoreRef.h>
#include <WebKit/WKWebsiteDataStoreConfigurationRef.h>

#include <algorithm>
#include <string>
#include <vector>

struct WB_VIEW {
    WKViewRef view { nullptr };
    HWND window { nullptr };
};

namespace {
std::string toUtf8(const wchar_t* value)
{
    if (!value || !*value)
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
        return {};
    std::string result(static_cast<size_t>(length), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), length, nullptr, nullptr))
        return {};
    result.resize(static_cast<size_t>(length - 1));
    return result;
}

int copyWKString(WKStringRef value, wchar_t* buffer, int capacity)
{
    if (!buffer || capacity <= 0)
        return 0;
    buffer[0] = L'\0';
    if (!value)
        return 0;

    const size_t maxBytes = WKStringGetMaximumUTF8CStringSize(value);
    std::vector<char> utf8(maxBytes);
    if (!WKStringGetUTF8CString(value, utf8.data(), maxBytes))
        return 0;

    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), -1, nullptr, 0);
    if (required <= 0)
        return 0;
    std::vector<wchar_t> wide(static_cast<size_t>(required));
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), -1, wide.data(), required))
        return 0;

    const int copied = std::min(required - 1, capacity - 1);
    std::copy_n(wide.data(), copied, buffer);
    buffer[copied] = L'\0';
    return required - 1;
}
}

WB_VIEW_HANDLE WB_Create(HWND parent, int x, int y, int width, int height, const wchar_t* initialUrl)
{
    if (!parent || !IsWindow(parent) || width <= 0 || height <= 0)
        return nullptr;

    auto dataStoreConfiguration = WKWebsiteDataStoreConfigurationCreate();
    if (!dataStoreConfiguration)
        return nullptr;
    auto dataStore = WKWebsiteDataStoreCreateWithConfiguration(dataStoreConfiguration);
    auto contextConfiguration = WKContextConfigurationCreate();
    auto context = contextConfiguration ? WKContextCreateWithConfiguration(contextConfiguration) : nullptr;
    auto preferences = WKPreferencesCreate();
    auto pageConfiguration = WKPageConfigurationCreate();

    if (!dataStore || !context || !preferences || !pageConfiguration) {
        if (pageConfiguration) WKRelease(pageConfiguration);
        if (preferences) WKRelease(preferences);
        if (context) WKRelease(context);
        if (contextConfiguration) WKRelease(contextConfiguration);
        if (dataStore) WKRelease(dataStore);
        WKRelease(dataStoreConfiguration);
        return nullptr;
    }

    WKPageConfigurationSetWebsiteDataStore(pageConfiguration, dataStore);
    WKPageConfigurationSetContext(pageConfiguration, context);
    WKPageConfigurationSetPreferences(pageConfiguration, preferences);

    RECT rect { x, y, x + width, y + height };
    auto nativeView = WKViewCreate(rect, pageConfiguration, parent);

    WKRelease(pageConfiguration);
    WKRelease(preferences);
    WKRelease(context);
    WKRelease(contextConfiguration);
    WKRelease(dataStore);
    WKRelease(dataStoreConfiguration);

    if (!nativeView)
        return nullptr;

    WKViewSetIsInWindow(nativeView, true);
    auto nativeWindow = WKViewGetWindow(nativeView);
    if (!nativeWindow || !IsWindow(nativeWindow)) {
        WKRelease(nativeView);
        return nullptr;
    }

    auto result = new (std::nothrow) WB_VIEW;
    if (!result) {
        WKViewSetIsInWindow(nativeView, false);
        WKRelease(nativeView);
        return nullptr;
    }

    result->view = nativeView;
    result->window = nativeWindow;
    SetWindowPos(nativeWindow, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(nativeWindow, SW_SHOW);
    if (initialUrl && *initialUrl)
        WB_Navigate(result, initialUrl);
    return result;
}

void WB_Destroy(WB_VIEW_HANDLE view)
{
    if (!view)
        return;
    if (view->view) {
        WKViewSetIsInWindow(view->view, false);
        WKRelease(view->view);
        view->view = nullptr;
    }
    view->window = nullptr;
    delete view;
}

void WB_SetBounds(WB_VIEW_HANDLE view, int x, int y, int width, int height)
{
    if (!view || !view->window || !IsWindow(view->window))
        return;
    if (width <= 0 || height <= 0) {
        ShowWindow(view->window, SW_HIDE);
        return;
    }
    SetWindowPos(view->window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void WB_Navigate(WB_VIEW_HANDLE view, const wchar_t* url)
{
    if (!view || !view->view || !url || !*url)
        return;
    const auto utf8 = toUtf8(url);
    if (utf8.empty())
        return;
    auto wkUrl = WKURLCreateWithUTF8CString(utf8.c_str());
    if (!wkUrl)
        return;
    WKPageLoadURL(WKViewGetPage(view->view), wkUrl);
    WKRelease(wkUrl);
}

void WB_GoBack(WB_VIEW_HANDLE view) { if (view && view->view) WKPageGoBack(WKViewGetPage(view->view)); }
void WB_GoForward(WB_VIEW_HANDLE view) { if (view && view->view) WKPageGoForward(WKViewGetPage(view->view)); }
void WB_Reload(WB_VIEW_HANDLE view) { if (view && view->view) WKPageReload(WKViewGetPage(view->view)); }
void WB_Stop(WB_VIEW_HANDLE view) { if (view && view->view) WKPageStopLoading(WKViewGetPage(view->view)); }
BOOL WB_CanGoBack(WB_VIEW_HANDLE view) { return view && view->view && WKPageCanGoBack(WKViewGetPage(view->view)); }
BOOL WB_CanGoForward(WB_VIEW_HANDLE view) { return view && view->view && WKPageCanGoForward(WKViewGetPage(view->view)); }

int WB_GetCurrentUrl(WB_VIEW_HANDLE view, wchar_t* buffer, int capacity)
{
    if (!view || !view->view)
        return 0;
    auto url = WKPageCopyActiveURL(WKViewGetPage(view->view));
    auto value = url ? WKURLCopyString(url) : nullptr;
    const int result = copyWKString(value, buffer, capacity);
    if (value) WKRelease(value);
    if (url) WKRelease(url);
    return result;
}

int WB_GetTitle(WB_VIEW_HANDLE view, wchar_t* buffer, int capacity)
{
    if (!view || !view->view)
        return 0;
    auto title = WKPageCopyTitle(WKViewGetPage(view->view));
    const int result = copyWKString(title, buffer, capacity);
    if (title) WKRelease(title);
    return result;
}
