# WinCairo integration

## Selected engine and supported platform

The application uses the upstream WebKit Windows port, WinCairo. WebKit's Windows-port documentation identifies Cairo as the graphics backend, libcurl as the network backend, and x64 Windows as the supported architecture. This is a native WebKit port; it is not Chromium, Gecko, WebView2, CEF, or Electron.

## Actual public C API used

The C++ bridge links against WebKit's public C API:

- `WKViewCreate` creates a native WebKit view with a parent HWND and pixel rectangle.
- `WKViewGetWindow` obtains the native child HWND; `WKViewSetIsInWindow` attaches/detaches it.
- `WKViewGetPage` obtains the page associated with a view.
- `WKPageConfigurationCreate`, `WKPageConfigurationSetContext`, `WKPageConfigurationSetPreferences`, and `WKPageConfigurationSetWebsiteDataStore` configure each page.
- `WKPageSetPageNavigationClient` installs `WKPageNavigationClientV3` callbacks for downloads initiated by navigation, response handling, and context menus.
- `WKDownloadSetClient` installs `WKDownloadClientV0` to select a safe destination and report progress, completion, and failure.
- `WKPageSetPageUIClient` installs `WKPageUIClientV13` callbacks. Geolocation and notification decisions are per-origin; JavaScript alert, confirm, prompt and before-unload callbacks are completed asynchronously by native WinUI `ContentDialog` surfaces; user-media requests are denied until a proper device-selection flow is implemented.
- `WKWebsiteDataStoreConfigurationCreate` plus the `WKWebsiteDataStoreConfigurationSet*Directory` functions establish persistent profile storage.

The bridge is implemented in `Native/WebKitHost`. C# calls its small C ABI through `Services/WebKitNative.cs`. The WinUI layer owns browser chrome and view models; C++ owns WKView handles and WebKit callback structures.

## Multi-tab profile model

All tabs share one `WKContextRef`, `WKPreferencesRef`, and persistent `WKWebsiteDataStoreRef`, so cookies and site data are shared between tabs. Profile data is stored below `%LOCALAPPDATA%\Blaze\WebKitData`. Each tab has its own WKView/page and native child HWND. Closing a tab releases its WKView; the shared context is released when the last view closes.

Downloads are written to the Windows Downloads known folder. Suggested filenames are sanitized, path components are stripped, and collisions receive a numeric suffix. Download callbacks copy their callback/user-data values into a per-download context so a tab can close without invalidating an in-flight download callback.

Geolocation and notification requests are denied unless the origin has an explicit allow setting. The first request is recorded and surfaced to the user so it can be allowed in Settings. Camera and microphone are denied by default because no device picker exists yet.

## Reproducible WebKit build

The GitHub Actions workflow pins the exact WebKit commit in `.github/workflows/build-windows.yml`. It builds on `windows-latest` with the x64 Visual Studio C++ workload and the toolchain required by the upstream Windows-port guide:

- Visual Studio C++ workload
- XAMPP-provided Perl
- Python 3.11
- Ruby
- CMake
- GnuWin32 gperf
- LLVM
- Ninja

Before checkout, Git long paths are enabled because the WebKit tree contains paths longer than the default Windows Git limit. `core.autocrlf=input` is also set to preserve source files that require LF line endings.

The official build entry point is `perl Tools/Scripts/build-webkit --release`. The script downloads the required WebKitRequirements package. A successful engine build is expected to produce `WebKitBuild/Release/bin64/MiniBrowser.exe`; the workflow uses this as a build-health check and stages the runtime DLLs/process helpers, not MiniBrowser itself.

## Important limitations

WinCairo is an upstream development/testing port and does not have the same regular security servicing and release cadence as WebKit's Apple and Igalia ports. A browser built from it must not be described as production-secure merely because it compiles.

Before calling this browser ready for general use, verify:

1. The pinned engine builds successfully and its revision is recorded with each release.
2. A clean Windows 10/11 machine can install the signed MSIX and launch the app with no developer tools installed.
3. WebKit subprocesses, resources, fonts, media, downloads, and site storage all resolve from the packaged runtime.
4. Navigation, redirects, TLS failures, certificates, popups, JavaScript dialogs, permission flows, downloads, and crash recovery are tested.
5. A routine upstream security-update process rebuilds and republishes the engine promptly.

Do not add an engine fallback. If WinCairo cannot be built or hosted, surface the failure and fix the WebKit integration.

## References

- [WebKit Windows port build guide](https://docs.webkit.org/Ports/WindowsPort.html)
- [WebKit Windows WKView header](https://github.com/WebKit/WebKit/blob/main/Source/WebKit/UIProcess/API/C/win/WKView.h)
- [WebKit Windows MiniBrowser host implementation](https://github.com/WebKit/WebKit/blob/main/Tools/MiniBrowser/win/WebKitBrowserWindow.cpp)


## Shared-surface protocol status

The experimental directory now contains a versioned, fixed-width descriptor ABI and a deterministic bounded-ring state machine test. The descriptor deliberately carries a view identifier, resize generation, adapter LUID, BGRA8 format, keyed-mutex contract, and a bounded set of legacy DXGI resource tokens. Frame notifications carry monotonically increasing sequence IDs and generation identity. The ring drops stale completed frames rather than allowing UI presentation to accumulate latency.

This is not yet wired into the C ABI or WebKit IPC. In particular, no shared resource handle is currently sent to a GPU process, no ANGLE surface is created from the descriptor, and no SwapChainPanel is attached. The current HWND path remains the only production presentation path.
