#include "WebKitHost.h"

#include <WebKit/WebKit2_C.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPagePrivate.h>
#include <WebKit/WKPreferencesRefPrivate.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKContext.h>
#include <WebKit/WKPreferencesRef.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKView.h>
#include <WebKit/WKWebsiteDataStoreRef.h>
#include <WebKit/WKHTTPCookieStore.h>
#include <WebKit/WKWebsiteDataStoreConfigurationRef.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKPageUIClient.h>
#include <WebKit/WKDownloadRef.h>
#include <WebKit/WKDownloadClient.h>
#include <WebKit/WKNavigationResponseRef.h>
#include <WebKit/WKURLResponse.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKSecurityOriginRef.h>
#include <WebKit/WKGeolocationPermissionRequest.h>
#include <WebKit/WKNotificationPermissionRequest.h>
#include <WebKit/WKUserMediaPermissionRequest.h>

#include <algorithm>
#include <new>
#include <string>
#include <vector>
#include <cwchar>
#include <system_error>
#include <filesystem>
#include <ShlObj.h>
#include <string>
#include <vector>

struct WB_VIEW {
    WKViewRef view { nullptr };
    HWND window { nullptr };
    WKPageNavigationClientV3 navigationClient { };
    WKPageUIClientV13 uiClient { };
    WB_DOWNLOAD_CALLBACK downloadCallback { nullptr };
    void* downloadUserData { nullptr };
    WB_PERMISSION_CALLBACK permissionCallback { nullptr };
    void* permissionUserData { nullptr };
};

struct DownloadContext {
    WB_DOWNLOAD_CALLBACK callback { nullptr };
    void* userData { nullptr };
    std::wstring destination;
    long long bytesWritten { 0 };
    long long bytesExpected { 0 };
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

WKWebsiteDataStoreRef sharedDataStore = nullptr;
WKContextRef sharedContext = nullptr;
WKPreferencesRef sharedPreferences = nullptr;
unsigned activeViewCount = 0;

void releaseSharedEngine()
{
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

    std::filesystem::path root = std::filesystem::path(localAppData) / L"WebKitBrowser" / L"WebKitData";
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

WB_VIEW_HANDLE WB_Create(HWND parent, int x, int y, int width, int height, const wchar_t*)
{
    if (!parent || !IsWindow(parent) || width <= 0 || height <= 0)
        return nullptr;

    if (!acquireSharedEngine())
        return nullptr;

    auto pageConfiguration = WKPageConfigurationCreate();
    if (!pageConfiguration) {
        releaseSharedEngine();
        return nullptr;
    }

    WKPageConfigurationSetWebsiteDataStore(pageConfiguration, sharedDataStore);
    WKPageConfigurationSetContext(pageConfiguration, sharedContext);
    WKPageConfigurationSetPreferences(pageConfiguration, sharedPreferences);

    RECT rect { x, y, x + width, y + height };
    auto nativeView = WKViewCreate(rect, pageConfiguration, parent);
    WKRelease(pageConfiguration);

    if (!nativeView) {
        releaseSharedEngine();
        return nullptr;
    }

    WKViewSetIsInWindow(nativeView, true);
    auto nativeWindow = WKViewGetWindow(nativeView);
    if (!nativeWindow || !IsWindow(nativeWindow)) {
        WKRelease(nativeView);
        releaseSharedEngine();
        return nullptr;
    }

    auto result = new (std::nothrow) WB_VIEW;
    if (!result) {
        WKViewSetIsInWindow(nativeView, false);
        WKRelease(nativeView);
        releaseSharedEngine();
        return nullptr;
    }

    result->view = nativeView;
    result->window = nativeWindow;

    auto page = WKViewGetPage(nativeView);
    result->navigationClient.base.version = 3;
    result->navigationClient.base.clientInfo = result;
    result->navigationClient.navigationActionDidBecomeDownload = navigationActionDidBecomeDownload;
    result->navigationClient.navigationResponseDidBecomeDownload = navigationResponseDidBecomeDownload;
    result->navigationClient.contextMenuDidCreateDownload = contextMenuDidCreateDownload;
    WKPageSetPageNavigationClient(page, &result->navigationClient.base);

    result->uiClient.base.version = 13;
    result->uiClient.base.clientInfo = result;
    result->uiClient.decidePolicyForGeolocationPermissionRequest = decideGeolocationPermission;
    result->uiClient.decidePolicyForNotificationPermissionRequest = decideNotificationPermission;
    result->uiClient.decidePolicyForUserMediaPermissionRequest = denyUserMediaPermission;
    WKPageSetPageUIClient(page, &result->uiClient.base);

    SetWindowPos(nativeWindow, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(nativeWindow, SW_SHOW);
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
    releaseSharedEngine();
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
