#include "WebKitHost.h"

#include <WebKit/WebKit2_C.h>
#include <WebKit/WKContext.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKDownloadClient.h>
#include <WebKit/WKDownloadRef.h>
#include <WebKit/WKGeolocationPermissionRequest.h>
#include <WebKit/WKHTTPCookieStoreRef.h>
#include <WebKit/WKNavigationResponseRef.h>
#include <WebKit/WKNotificationPermissionRequest.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKPagePrivate.h>
#include <WebKit/WKPageUIClient.h>
#include <WebKit/WKPreferencesRef.h>
#include <WebKit/WKPreferencesRefPrivate.h>
#include <WebKit/WKSecurityOriginRef.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKURLResponse.h>
#include <WebKit/WKUserMediaPermissionRequest.h>
#include <WebKit/WKView.h>
#include <WebKit/WKWebsiteDataStoreConfigurationRef.h>
#include <WebKit/WKWebsiteDataStoreRef.h>

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <mutex>
#include <cwchar>
#include <filesystem>
#include <new>
#include <string>
#include <system_error>
#include <vector>
#include <ShlObj.h>

struct DownloadContext {
    WB_DOWNLOAD_CALLBACK callback{};
    void* userData{};
    std::wstring destination;
    long long bytesWritten{};
    long long bytesExpected{};
};

struct WB_VIEW {
    WKViewRef view{};
    HWND window{};
    WKPageNavigationClientV3 navigationClient{};
    WKPageUIClientV13 uiClient{};
    WB_DOWNLOAD_CALLBACK downloadCallback{};
    void* downloadUserData{};
    WB_PERMISSION_CALLBACK permissionCallback{};
    void* permissionUserData{};
    WB_JAVASCRIPT_DIALOG_CALLBACK javaScriptDialogCallback{};
    void* javaScriptDialogUserData{};
    WB_NEW_PAGE_CALLBACK newPageCallback{};
    WB_CLOSE_PAGE_CALLBACK closePageCallback{};
    void* popupUserData{};
    HWND parentWindow{};
    HWND hostWindow{};
    int x{};
    int y{};
    int width{};
    int height{};
    UINT dpi{};
    bool ownerSubclassInstalled{};
    bool destroyPrepared{};
};

std::wstring fromWKString(WKStringRef value);

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

WKWebsiteDataStoreRef sharedDataStore = nullptr;
WKContextRef sharedContext = nullptr;
WKPreferencesRef sharedPreferences = nullptr;
unsigned activeViewCount = 0;
std::mutex sharedEngineMutex;

void releaseSharedEngine()
{
    std::lock_guard<std::mutex> lock(sharedEngineMutex);
    if (activeViewCount > 0)
        --activeViewCount;
    if (activeViewCount != 0)
        return;
    if (sharedPreferences) WKRelease(sharedPreferences);
    if (sharedContext) WKRelease(sharedContext);
    if (sharedDataStore) WKRelease(sharedDataStore);
    sharedPreferences = nullptr;
    sharedContext = nullptr;
    sharedDataStore = nullptr;
}

bool configurePersistentWebsiteDataStore(WKWebsiteDataStoreConfigurationRef configuration)
{
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData)) || !localAppData)
        return false;

    std::filesystem::path root = std::filesystem::path(localAppData) / L"Blaze" / L"WebKitData";
    CoTaskMemFree(localAppData);

    struct DirectorySetting {
        const wchar_t* name;
        void (*set)(WKWebsiteDataStoreConfigurationRef, WKStringRef);
    };
    const DirectorySetting settings[] = {
        { L"General", WKWebsiteDataStoreConfigurationSetGeneralStorageDirectory },
        { L"NetworkCache", WKWebsiteDataStoreConfigurationSetNetworkCacheDirectory },
        { L"IndexedDB", WKWebsiteDataStoreConfigurationSetIndexedDBDatabaseDirectory },
        { L"LocalStorage", WKWebsiteDataStoreConfigurationSetLocalStorageDirectory },
        { L"CacheStorage", WKWebsiteDataStoreConfigurationSetCacheStorageDirectory },
        { L"ServiceWorkers", WKWebsiteDataStoreConfigurationSetServiceWorkerRegistrationDirectory },
        { L"ResourceLoadStatistics", WKWebsiteDataStoreConfigurationSetResourceLoadStatisticsDirectory },
        { L"MediaKeys", WKWebsiteDataStoreConfigurationSetMediaKeysStorageDirectory },
        { L"WebSQL", WKWebsiteDataStoreConfigurationSetWebSQLDatabaseDirectory }
    };

    std::error_code error;
    for (const auto& setting : settings) {
        auto directory = root / setting.name;
        std::filesystem::create_directories(directory, error);
        if (error)
            return false;
        const auto utf8 = toUtf8(directory.wstring().c_str());
        auto wkPath = WKStringCreateWithUTF8CString(utf8.c_str());
        if (!wkPath)
            return false;
        setting.set(configuration, wkPath);
        WKRelease(wkPath);
    }

    auto cookieDirectory = root / L"Cookies";
    std::filesystem::create_directories(cookieDirectory, error);
    if (error)
        return false;
    const auto cookiePath = toUtf8((cookieDirectory / L"Cookies.binarycookies").wstring().c_str());
    auto wkCookiePath = WKStringCreateWithUTF8CString(cookiePath.c_str());
    if (!wkCookiePath)
        return false;
    WKWebsiteDataStoreConfigurationSetCookieStorageFile(configuration, wkCookiePath);
    WKRelease(wkCookiePath);
    return true;
}

bool acquireSharedEngine()
{
    std::lock_guard<std::mutex> lock(sharedEngineMutex);
    if (!sharedDataStore || !sharedContext || !sharedPreferences) {
        auto storeConfiguration = WKWebsiteDataStoreConfigurationCreate();
        auto contextConfiguration = WKContextConfigurationCreate();
        const bool storageConfigured = storeConfiguration && configurePersistentWebsiteDataStore(storeConfiguration);
        auto store = storageConfigured ? WKWebsiteDataStoreCreateWithConfiguration(storeConfiguration) : nullptr;
        auto context = contextConfiguration ? WKContextCreateWithConfiguration(contextConfiguration) : nullptr;
        auto preferences = WKPreferencesCreate();

        if (!store || !context || !preferences) {
            if (preferences) WKRelease(preferences);
            if (context) WKRelease(context);
            if (store) WKRelease(store);
            if (contextConfiguration) WKRelease(contextConfiguration);
            if (storeConfiguration) WKRelease(storeConfiguration);
            return false;
        }

        sharedDataStore = store;
        sharedContext = context;
        // Route every script-created window request through the native WinUI popup policy.
        WKPreferencesSetJavaScriptCanOpenWindowsAutomatically(preferences, true);
        sharedPreferences = preferences;
        WKContextSetCacheModel(sharedContext, kWKCacheModelPrimaryWebBrowser);

        WKRelease(contextConfiguration);
        WKRelease(storeConfiguration);
    }
    ++activeViewCount;
    return true;
}
}

std::wstring fromWKString(WKStringRef value)
{
    std::vector<wchar_t> buffer(4096);
    const int length = copyWKString(value, buffer.data(), static_cast<int>(buffer.size()));
    return length > 0 ? std::wstring(buffer.data()) : std::wstring();
}

std::wstring downloadsDirectory()
{
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_CREATE, nullptr, &path)) || !path)
        return {};
    std::wstring result(path);
    CoTaskMemFree(path);
    return result;
}

void notifyDownload(DownloadContext* context, int status, long long written, long long expected)
{
    if (context && context->callback && !context->destination.empty())
        context->callback(context->destination.c_str(), status, written, expected, context->userData);
}

WKStringRef downloadDecideDestination(WKDownloadRef, WKURLResponseRef response, WKStringRef suggestedFilename, const void* clientInfo)
{
    auto* context = const_cast<DownloadContext*>(static_cast<const DownloadContext*>(clientInfo));
    auto filename = fromWKString(suggestedFilename);
    if (filename.empty() && response) {
        auto url = WKURLResponseCopyURL(response);
        auto host = url ? WKURLCopyHostName(url) : nullptr;
        filename = fromWKString(host);
        if (host) WKRelease(host);
        if (url) WKRelease(url);
    }
    if (filename.empty())
        filename = L"download";

    for (auto& character : filename) {
        if (character < 32 || wcschr(L"<>:\"/\\|?*", character))
            character = L'_';
    }

    const auto folder = downloadsDirectory();
    if (folder.empty())
        return nullptr;

    std::filesystem::path target = std::filesystem::path(folder) / std::filesystem::path(filename).filename();
    std::error_code error;
    const auto original = target;
    unsigned suffix = 1;
    while (std::filesystem::exists(target, error)) {
        target = original.parent_path() /
            (original.stem().wstring() + L" (" + std::to_wstring(suffix++) + L")" + original.extension().wstring());
        error.clear();
    }

    context->destination = target.wstring();
    notifyDownload(context, 0, 0, 0);
    const auto utf8 = toUtf8(context->destination.c_str());
    return utf8.empty() ? nullptr : WKStringCreateWithUTF8CString(utf8.c_str());
}

void downloadDidWriteData(WKDownloadRef, long long, long long totalBytesWritten, long long totalBytesExpectedToWrite, const void* clientInfo)
{
    auto* context = const_cast<DownloadContext*>(static_cast<const DownloadContext*>(clientInfo));
    context->bytesWritten = totalBytesWritten;
    context->bytesExpected = totalBytesExpectedToWrite;
    notifyDownload(context, 1, context->bytesWritten, context->bytesExpected);
}

void downloadDidFinish(WKDownloadRef, const void* clientInfo)
{
    auto* context = const_cast<DownloadContext*>(static_cast<const DownloadContext*>(clientInfo));
    notifyDownload(context, 2, context->bytesWritten, context->bytesExpected);
    delete context;
}

void downloadDidFail(WKDownloadRef, WKErrorRef, WKDataRef, const void* clientInfo)
{
    auto* context = const_cast<DownloadContext*>(static_cast<const DownloadContext*>(clientInfo));
    notifyDownload(context, 3, context->bytesWritten, context->bytesExpected);
    delete context;
}

void attachDownload(WKDownloadRef download, const void* clientInfo)
{
    auto* view = const_cast<WB_VIEW*>(static_cast<const WB_VIEW*>(clientInfo));
    auto* context = new (std::nothrow) DownloadContext;
    if (!context)
        return;
    context->callback = view->downloadCallback;
    context->userData = view->downloadUserData;

    WKDownloadClientV0 client { };
    client.base.version = 0;
    client.base.clientInfo = context;
    client.decideDestinationWithResponse = downloadDecideDestination;
    client.didWriteData = downloadDidWriteData;
    client.didFinish = downloadDidFinish;
    client.didFailWithError = downloadDidFail;
    WKDownloadSetClient(download, &client.base);
}

void navigationActionDidBecomeDownload(WKPageRef, WKNavigationActionRef, WKDownloadRef download, const void* clientInfo)
{
    attachDownload(download, clientInfo);
}

void navigationResponseDidBecomeDownload(WKPageRef, WKNavigationResponseRef, WKDownloadRef download, const void* clientInfo)
{
    attachDownload(download, clientInfo);
}

void contextMenuDidCreateDownload(WKPageRef, WKDownloadRef download, const void* clientInfo)
{
    attachDownload(download, clientInfo);
}

void decideGeolocationPermission(WKPageRef, WKFrameRef, WKSecurityOriginRef origin, WKGeolocationPermissionRequestRef request, const void* clientInfo)
{
    auto* view = const_cast<WB_VIEW*>(static_cast<const WB_VIEW*>(clientInfo));
    auto originString = WKSecurityOriginCopyToString(origin);
    auto text = fromWKString(originString);
    if (originString) WKRelease(originString);
    const bool allowed = view->permissionCallback && view->permissionCallback(text.c_str(), 0, view->permissionUserData) != 0;
    if (allowed) WKGeolocationPermissionRequestAllow(request);
    else WKGeolocationPermissionRequestDeny(request);
}

void decideNotificationPermission(WKPageRef, WKSecurityOriginRef origin, WKNotificationPermissionRequestRef request, const void* clientInfo)
{
    auto* view = const_cast<WB_VIEW*>(static_cast<const WB_VIEW*>(clientInfo));
    auto originString = WKSecurityOriginCopyToString(origin);
    auto text = fromWKString(originString);
    if (originString) WKRelease(originString);
    const bool allowed = view->permissionCallback && view->permissionCallback(text.c_str(), 1, view->permissionUserData) != 0;
    if (allowed) WKNotificationPermissionRequestAllow(request);
    else WKNotificationPermissionRequestDeny(request);
}

void denyUserMediaPermission(WKPageRef, WKFrameRef, WKSecurityOriginRef, WKSecurityOriginRef topLevelOrigin, WKUserMediaPermissionRequestRef request, const void* clientInfo)
{
    auto* view = const_cast<WB_VIEW*>(static_cast<const WB_VIEW*>(clientInfo));
    auto originString = WKSecurityOriginCopyToString(topLevelOrigin);
    auto text = fromWKString(originString);
    if (originString) WKRelease(originString);
    if (view->permissionCallback)
        view->permissionCallback(text.c_str(), 2, view->permissionUserData);
    WKUserMediaPermissionRequestDeny(request, kWKPermissionDenied);
}

void runJavaScriptAlert(WKPageRef, WKStringRef alertText, WKFrameRef, WKSecurityOriginRef securityOrigin, WKPageRunJavaScriptAlertResultListenerRef listener, const void* clientInfo)
{
    auto* view = const_cast<WB_VIEW*>(static_cast<const WB_VIEW*>(clientInfo));
    auto originString = WKSecurityOriginCopyToString(securityOrigin);
    auto origin = fromWKString(originString);
    if (originString) WKRelease(originString);
    auto message = fromWKString(alertText);
    if (!view->javaScriptDialogCallback) {
        WKPageRunJavaScriptAlertResultListenerCall(listener);
        return;
    }
    WKRetain(reinterpret_cast<WKTypeRef>(listener));
    view->javaScriptDialogCallback(0, origin.c_str(), message.c_str(), L"", listener, view->javaScriptDialogUserData);
}

void runJavaScriptConfirm(WKPageRef, WKStringRef messageValue, WKFrameRef, WKSecurityOriginRef securityOrigin, WKPageRunJavaScriptConfirmResultListenerRef listener, const void* clientInfo)
{
    auto* view = const_cast<WB_VIEW*>(static_cast<const WB_VIEW*>(clientInfo));
    auto originString = WKSecurityOriginCopyToString(securityOrigin);
    auto origin = fromWKString(originString);
    if (originString) WKRelease(originString);
    auto message = fromWKString(messageValue);
    if (!view->javaScriptDialogCallback) {
        WKPageRunJavaScriptConfirmResultListenerCall(listener, false);
        return;
    }
    WKRetain(reinterpret_cast<WKTypeRef>(listener));
    view->javaScriptDialogCallback(1, origin.c_str(), message.c_str(), L"", listener, view->javaScriptDialogUserData);
}

void runJavaScriptPrompt(WKPageRef, WKStringRef messageValue, WKStringRef defaultValue, WKFrameRef, WKSecurityOriginRef securityOrigin, WKPageRunJavaScriptPromptResultListenerRef listener, const void* clientInfo)
{
    auto* view = const_cast<WB_VIEW*>(static_cast<const WB_VIEW*>(clientInfo));
    auto originString = WKSecurityOriginCopyToString(securityOrigin);
    auto origin = fromWKString(originString);
    if (originString) WKRelease(originString);
    auto message = fromWKString(messageValue);
    auto initialValue = fromWKString(defaultValue);
    if (!view->javaScriptDialogCallback) {
        WKPageRunJavaScriptPromptResultListenerCall(listener, nullptr);
        return;
    }
    WKRetain(reinterpret_cast<WKTypeRef>(listener));
    view->javaScriptDialogCallback(2, origin.c_str(), message.c_str(), initialValue.c_str(), listener, view->javaScriptDialogUserData);
}

void runBeforeUnloadConfirmPanel(WKPageRef, WKStringRef messageValue, WKFrameRef, WKPageRunBeforeUnloadConfirmPanelResultListenerRef listener, const void* clientInfo)
{
    auto* view = const_cast<WB_VIEW*>(static_cast<const WB_VIEW*>(clientInfo));
    auto message = fromWKString(messageValue);
    if (!view->javaScriptDialogCallback) {
        WKPageRunBeforeUnloadConfirmPanelResultListenerCall(listener, false);
        return;
    }
    WKRetain(reinterpret_cast<WKTypeRef>(listener));
    view->javaScriptDialogCallback(3, L"", message.c_str(), L"", listener, view->javaScriptDialogUserData);
}

namespace {
WB_VIEW_HANDLE createNativeView(HWND parent, int x, int y, int width, int height,
    WKPageConfigurationRef pageConfiguration, const WB_VIEW* opener, bool showImmediately);

constexpr wchar_t NativeSurfaceHostClass[] = L"Blaze.WebKit.NativeSurfaceHost";

bool ensureNativeSurfaceHostClass()
{
    static std::once_flag registration;
    static bool registered = false;
    std::call_once(registration, [] {
        WNDCLASSEXW windowClass {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = NativeSurfaceHostClass;
        registered = RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    });
    return registered;
}

UINT_PTR ownerSubclassId(const WB_VIEW* view)
{
    return reinterpret_cast<UINT_PTR>(view);
}

HWND ownedPopupInsertAfter(HWND owner, HWND popup)
{
    auto previous = GetWindow(owner, GW_HWNDPREV);
    while (previous && (previous == popup
        || (GetWindowLongPtr(previous, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0))
        previous = GetWindow(previous, GW_HWNDPREV);
    // HWND_TOP means the top of the non-topmost group, preserving any
    // unrelated topmost windows while keeping this owned popup above owner.
    return previous ? previous : HWND_TOP;
}

LRESULT CALLBACK ownerWindowSubclass(HWND owner, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR referenceData)
{
    auto* view = reinterpret_cast<WB_VIEW*>(referenceData);
    const auto result = DefSubclassProc(owner, message, wParam, lParam);

    // Owned popup hosts are top-level windows, so WinUI's DirectComposition
    // surface cannot cover them. Keep each host aligned as its owner moves
    // between screens without relying on a XAML SizeChanged event.
    if (view && view->hostWindow && view->width > 0 && view->height > 0
        && (message == WM_WINDOWPOSCHANGED || message == WM_DPICHANGED)) {
        POINT origin { view->x, view->y };
        if (ClientToScreen(owner, &origin)) {
            SetWindowPos(view->hostWindow, ownedPopupInsertAfter(owner, view->hostWindow),
                origin.x, origin.y, view->width, view->height,
                SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
            const UINT dpi = GetDpiForWindow(view->hostWindow);
            if (dpi && dpi != view->dpi && view->view) {
                view->dpi = dpi;
                WKPageSetCustomBackingScaleFactor(WKViewGetPage(view->view),
                    std::round(static_cast<double>(dpi) / 96.0));
            }
        }
    }

    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(owner, ownerWindowSubclass, subclassId);
    return result;
}



WKPageRef createNewPage(WKPageRef, WKPageConfigurationRef pageConfiguration,
    WKNavigationActionRef, WKWindowFeaturesRef, const void* clientInfo)
{
    auto* opener = const_cast<WB_VIEW*>(static_cast<const WB_VIEW*>(clientInfo));
    if (!opener || !opener->parentWindow || !IsWindow(opener->parentWindow) || !acquireSharedEngine())
        return nullptr;

    auto* popup = createNativeView(opener->parentWindow, 0, 0, 1, 1, pageConfiguration, opener, false);
    if (!popup) {
        releaseSharedEngine();
        return nullptr;
    }

    if (!popup->newPageCallback || popup->newPageCallback(popup, popup->popupUserData) == 0) {
        WB_Destroy(popup);
        return nullptr;
    }

    auto page = WKViewGetPage(popup->view);
    if (!page) {
        WB_Destroy(popup);
        return nullptr;
    }
    WKRetain(reinterpret_cast<WKTypeRef>(page));
    return page;
}

void closePage(WKPageRef, const void* clientInfo)
{
    auto* view = const_cast<WB_VIEW*>(static_cast<const WB_VIEW*>(clientInfo));
    if (view && !view->destroyPrepared && view->closePageCallback)
        view->closePageCallback(view, view->popupUserData);
}

void didNotHandleKeyEvent(WKPageRef, WKNativeEventPtr event, const void* clientInfo)
{
    auto* view = const_cast<WB_VIEW*>(static_cast<const WB_VIEW*>(clientInfo));
    if (!view || view->destroyPrepared || !event || !view->parentWindow || !IsWindow(view->parentWindow))
        return;
    PostMessageW(view->parentWindow, event->message, event->wParam, event->lParam);
}

WB_VIEW_HANDLE createNativeView(HWND parent, int x, int y, int width, int height,
    WKPageConfigurationRef pageConfiguration, const WB_VIEW* opener, bool showImmediately)
{
    if (!parent || !IsWindow(parent) || width <= 0 || height <= 0 || !pageConfiguration)
        return nullptr;

    if (!parent || !IsWindow(parent) || !ensureNativeSurfaceHostClass())
        return nullptr;

    POINT screenOrigin { x, y };
    if (!ClientToScreen(parent, &screenOrigin))
        return nullptr;

    // A borderless owned popup is a separate top-level HWND. Unlike a child
    // HWND, it is not covered by WinUI's final DirectComposition surface.
    auto hostWindow = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        NativeSurfaceHostClass,
        L"",
        WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        screenOrigin.x,
        screenOrigin.y,
        width,
        height,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (!hostWindow)
        return nullptr;

    RECT rect { 0, 0, width, height };
    auto nativeView = WKViewCreate(rect, pageConfiguration, hostWindow);
    if (!nativeView) {
        DestroyWindow(hostWindow);
        return nullptr;
    }

    WKViewSetIsInWindow(nativeView, true);
    auto nativeWindow = WKViewGetWindow(nativeView);
    if (!nativeWindow || !IsWindow(nativeWindow)) {
        WKViewSetIsInWindow(nativeView, false);
        WKRelease(nativeView);
        DestroyWindow(hostWindow);
        return nullptr;
    }

    auto result = new (std::nothrow) WB_VIEW;
    if (!result) {
        WKViewSetIsInWindow(nativeView, false);
        WKRelease(nativeView);
        DestroyWindow(hostWindow);
        return nullptr;
    }

    result->view = nativeView;
    result->window = nativeWindow;
    result->parentWindow = parent;
    result->hostWindow = hostWindow;
    result->x = showImmediately ? x : 0;
    result->y = showImmediately ? y : 0;
    result->width = showImmediately ? width : 0;
    result->height = showImmediately ? height : 0;
    if (opener) {
        result->downloadCallback = opener->downloadCallback;
        result->downloadUserData = opener->downloadUserData;
        result->permissionCallback = opener->permissionCallback;
        result->permissionUserData = opener->permissionUserData;
        result->javaScriptDialogCallback = opener->javaScriptDialogCallback;
        result->javaScriptDialogUserData = opener->javaScriptDialogUserData;
        result->newPageCallback = opener->newPageCallback;
        result->closePageCallback = opener->closePageCallback;
        result->popupUserData = opener->popupUserData;
    }

    auto page = WKViewGetPage(nativeView);
    if (!page) {
        WKViewSetIsInWindow(nativeView, false);
        WKRelease(nativeView);
        DestroyWindow(hostWindow);
        delete result;
        return nullptr;
    }

    // Match the upstream WinCairo MiniBrowser host: explicitly set WebKit's
    // backing scale to the rounded native-window DPI scale.
    const UINT dpi = GetDpiForWindow(hostWindow);
    result->dpi = dpi;
    if (dpi)
        WKPageSetCustomBackingScaleFactor(page, std::round(static_cast<double>(dpi) / 96.0));

    result->navigationClient.base.version = 3;
    result->navigationClient.base.clientInfo = result;
    result->navigationClient.navigationActionDidBecomeDownload = navigationActionDidBecomeDownload;
    result->navigationClient.navigationResponseDidBecomeDownload = navigationResponseDidBecomeDownload;
    result->navigationClient.contextMenuDidCreateDownload = contextMenuDidCreateDownload;
    WKPageSetPageNavigationClient(page, &result->navigationClient.base);

    result->uiClient.base.version = 13;
    result->uiClient.base.clientInfo = result;
    result->uiClient.createNewPage = createNewPage;
    result->uiClient.close = closePage;
    result->uiClient.didNotHandleKeyEvent = didNotHandleKeyEvent;
    result->uiClient.decidePolicyForGeolocationPermissionRequest = decideGeolocationPermission;
    result->uiClient.decidePolicyForNotificationPermissionRequest = decideNotificationPermission;
    result->uiClient.decidePolicyForUserMediaPermissionRequest = denyUserMediaPermission;
    result->uiClient.runJavaScriptAlert = runJavaScriptAlert;
    result->uiClient.runJavaScriptConfirm = runJavaScriptConfirm;
    result->uiClient.runJavaScriptPrompt = runJavaScriptPrompt;
    result->uiClient.runBeforeUnloadConfirmPanel = runBeforeUnloadConfirmPanel;
    WKPageSetPageUIClient(page, &result->uiClient.base);

    if (!SetWindowSubclass(parent, ownerWindowSubclass, ownerSubclassId(result),
        reinterpret_cast<DWORD_PTR>(result))) {
        WKViewSetIsInWindow(nativeView, false);
        WKRelease(nativeView);
        DestroyWindow(hostWindow);
        delete result;
        return nullptr;
    }
    result->ownerSubclassInstalled = true;

    if (showImmediately) {
        SetWindowPos(hostWindow, ownedPopupInsertAfter(parent, hostWindow),
            screenOrigin.x, screenOrigin.y, width, height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
        SetWindowPos(nativeWindow, nullptr, 0, 0, width, height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ShowWindow(hostWindow, SW_SHOWNA);
    } else {
        ShowWindow(hostWindow, SW_HIDE);
        ShowWindow(nativeWindow, SW_HIDE);
    }
    return result;
}
}

WB_VIEW_HANDLE WB_Create(HWND parent, int x, int y, int width, int height, const wchar_t* initialUrl)
{
    if (!parent || !IsWindow(parent) || width <= 0 || height <= 0 || !acquireSharedEngine())
        return nullptr;

    auto pageConfiguration = WKPageConfigurationCreate();
    if (!pageConfiguration) {
        releaseSharedEngine();
        return nullptr;
    }

    WKPageConfigurationSetWebsiteDataStore(pageConfiguration, sharedDataStore);
    WKPageConfigurationSetContext(pageConfiguration, sharedContext);
    WKPageConfigurationSetPreferences(pageConfiguration, sharedPreferences);

    // Create the owned host hidden. LayoutNativeViews will show only the selected tab.
    auto result = createNativeView(parent, x, y, width, height, pageConfiguration, nullptr, false);
    WKRelease(pageConfiguration);
    if (!result) {
        releaseSharedEngine();
        return nullptr;
    }

    // The WinUI host installs native callbacks before starting the first navigation.
    (void)initialUrl;
    return result;
}

void WB_PrepareDestroy(WB_VIEW_HANDLE view)
{
    if (!view || view->destroyPrepared)
        return;
    view->destroyPrepared = true;
    if (view->ownerSubclassInstalled && view->parentWindow && IsWindow(view->parentWindow)) {
        RemoveWindowSubclass(view->parentWindow, ownerWindowSubclass, ownerSubclassId(view));
        view->ownerSubclassInstalled = false;
    }
    if (view->hostWindow && IsWindow(view->hostWindow))
        ShowWindow(view->hostWindow, SW_HIDE);
    if (view->view) {
        auto page = WKViewGetPage(view->view);
        if (page) {
            WKPageSetPageUIClient(page, nullptr);
            WKPageSetPageNavigationClient(page, nullptr);
            WKPageStopLoading(page);
        }
        WKViewSetIsInWindow(view->view, false);
    }
}

void WB_Destroy(WB_VIEW_HANDLE view)
{
    if (!view)
        return;
    WB_PrepareDestroy(view);
    if (view->view) {
        WKRelease(view->view);
        view->view = nullptr;
    }
    if (view->hostWindow && IsWindow(view->hostWindow))
        DestroyWindow(view->hostWindow);
    view->window = nullptr;
    view->hostWindow = nullptr;
    delete view;
    releaseSharedEngine();
}

BOOL WB_SetBounds(WB_VIEW_HANDLE view, int x, int y, int width, int height)
{
    if (!view || !view->window || !IsWindow(view->window) || !view->hostWindow || !IsWindow(view->hostWindow)
        || !view->parentWindow || !IsWindow(view->parentWindow))
        return FALSE;
    if (width <= 0 || height <= 0) {
        view->width = 0;
        view->height = 0;
        ShowWindow(view->hostWindow, SW_HIDE);
        return TRUE;
    }

    view->x = x;
    view->y = y;
    view->width = width;
    view->height = height;

    POINT screenOrigin { x, y };
    if (!ClientToScreen(view->parentWindow, &screenOrigin))
        return FALSE;

    if (!SetWindowPos(view->hostWindow, ownedPopupInsertAfter(view->parentWindow, view->hostWindow),
        screenOrigin.x, screenOrigin.y, width, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER))
        return FALSE;

    // Refresh the backing scale only when the owned host changes monitors.
    const UINT dpi = GetDpiForWindow(view->hostWindow);
    if (dpi && dpi != view->dpi && view->view) {
        view->dpi = dpi;
        WKPageSetCustomBackingScaleFactor(WKViewGetPage(view->view),
            std::round(static_cast<double>(dpi) / 96.0));
    }
    if (!SetWindowPos(view->window, nullptr, 0, 0, width, height,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW))
        return FALSE;
    ShowWindow(view->hostWindow, SW_SHOWNA);
    return TRUE;
}

HWND WB_GetWindow(WB_VIEW_HANDLE view)
{
    return view ? view->window : nullptr;
}

HWND WB_GetHostWindow(WB_VIEW_HANDLE view)
{
    return view ? view->hostWindow : nullptr;
}

void WB_SetUserAgent(WB_VIEW_HANDLE view, const wchar_t* userAgent)
{
    if (!view || !view->view || !userAgent || !*userAgent)
        return;
    const auto utf8 = toUtf8(userAgent);
    if (utf8.empty())
        return;
    auto value = WKStringCreateWithUTF8CString(utf8.c_str());
    if (!value)
        return;
    WKPageSetCustomUserAgent(WKViewGetPage(view->view), value);
    WKRelease(value);
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
double WB_GetEstimatedProgress(WB_VIEW_HANDLE view)
{
    if (!view || !view->view) return 1.0;
    return WKPageGetEstimatedProgress(WKViewGetPage(view->view));
}

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

void WB_SetJavaScriptEnabled(WB_VIEW_HANDLE view, BOOL enabled)
{
    if (!view || !view->view)
        return;
    auto configuration = WKPageCopyPageConfiguration(WKViewGetPage(view->view));
    if (!configuration)
        return;
    auto preferences = WKPageConfigurationGetPreferences(configuration);
    if (preferences)
        WKPreferencesSetJavaScriptEnabled(preferences, enabled != FALSE);
    WKRelease(configuration);
}

void WB_SetLoadsImagesAutomatically(WB_VIEW_HANDLE view, BOOL enabled)
{
    if (!view || !view->view)
        return;
    auto configuration = WKPageCopyPageConfiguration(WKViewGetPage(view->view));
    if (!configuration)
        return;
    auto preferences = WKPageConfigurationGetPreferences(configuration);
    if (preferences)
        WKPreferencesSetLoadsImagesAutomatically(preferences, enabled != FALSE);
    WKRelease(configuration);
}

void WB_ClearWebsiteData(WB_VIEW_HANDLE view)
{
    if (!view || !view->view)
        return;
    auto store = WKPageGetWebsiteDataStore(WKViewGetPage(view->view));
    if (!store)
        return;
    auto cookies = WKWebsiteDataStoreGetHTTPCookieStore(store);
    if (cookies)
        WKHTTPCookieStoreDeleteAllCookies(cookies, nullptr, [](void*) { });
    WKWebsiteDataStoreRemoveAllFetchCaches(store, nullptr, [](void*) { });
    WKWebsiteDataStoreRemoveNetworkCache(store, nullptr, [](void*) { });
    WKWebsiteDataStoreRemoveMemoryCaches(store, nullptr, [](void*) { });
    WKWebsiteDataStoreRemoveAllServiceWorkerRegistrations(store, nullptr, [](void*) { });
    WKWebsiteDataStoreRemoveAllIndexedDatabases(store, nullptr, [](void*) { });
    WKWebsiteDataStoreRemoveLocalStorage(store, nullptr, [](void*) { });
}

void WB_SetDownloadCallback(WB_VIEW_HANDLE view, WB_DOWNLOAD_CALLBACK callback, void* userData)
{
    if (!view)
        return;
    view->downloadCallback = callback;
    view->downloadUserData = userData;
}

void WB_SetPermissionCallback(WB_VIEW_HANDLE view, WB_PERMISSION_CALLBACK callback, void* userData)
{
    if (!view)
        return;
    view->permissionCallback = callback;
    view->permissionUserData = userData;
}

void WB_SetJavaScriptDialogCallback(WB_VIEW_HANDLE view, WB_JAVASCRIPT_DIALOG_CALLBACK callback, void* userData)
{
    if (!view)
        return;
    view->javaScriptDialogCallback = callback;
    view->javaScriptDialogUserData = userData;
}

void WB_SetPopupCallbacks(WB_VIEW_HANDLE view, WB_NEW_PAGE_CALLBACK newPageCallback,
    WB_CLOSE_PAGE_CALLBACK closePageCallback, void* userData)
{
    if (!view)
        return;
    view->newPageCallback = newPageCallback;
    view->closePageCallback = closePageCallback;
    view->popupUserData = userData;
}

void WB_ResolveJavaScriptDialog(const void* listener, int dialogType, BOOL accepted, const wchar_t* response)
{
    if (!listener)
        return;
    switch (dialogType) {
    case 0:
        WKPageRunJavaScriptAlertResultListenerCall(reinterpret_cast<WKPageRunJavaScriptAlertResultListenerRef>(listener));
        break;
    case 1:
        WKPageRunJavaScriptConfirmResultListenerCall(reinterpret_cast<WKPageRunJavaScriptConfirmResultListenerRef>(listener), accepted != FALSE);
        break;
    case 2: {
        WKStringRef value = nullptr;
        if (accepted && response) {
            const auto utf8 = toUtf8(response);
            value = WKStringCreateWithUTF8CString(utf8.c_str());
        }
        WKPageRunJavaScriptPromptResultListenerCall(reinterpret_cast<WKPageRunJavaScriptPromptResultListenerRef>(listener), value);
        if (value) WKRelease(value);
        break;
    }
    case 3:
        WKPageRunBeforeUnloadConfirmPanelResultListenerCall(reinterpret_cast<WKPageRunBeforeUnloadConfirmPanelResultListenerRef>(listener), accepted != FALSE);
        break;
    default:
        break;
    }
    WKRelease(reinterpret_cast<WKTypeRef>(listener));
}
