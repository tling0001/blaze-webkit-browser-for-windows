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


## Live WinCairo composition integration: source findings and remaining work

The pinned source has a concrete, narrow GPU-process call chain:

1. 'DrawingAreaWC::updateRendering' builds 'WCUpdateInfo' and calls 'RemoteWCLayerTreeHostProxy::update'.
2. 'RemoteWCLayerTreeHostProxy::ensureGPUProcessConnection' sends 'GPUConnectionToWebProcess::CreateWCLayerTreeHost(identifier, nativeWindowHandle, usesOffscreenRendering)'.
3. 'GPUConnectionToWebProcess::createWCLayerTreeHost' creates 'RemoteWCLayerTreeHost' in the GPU process.
4. 'RemoteWCLayerTreeHost' creates one shared 'WCSceneContext' per native window. 'WCSceneContext' currently calls 'GLContext::create(PlatformDisplay::sharedDisplay(), HWND)'.
5. 'WCScene::update' applies the TextureMapper layer tree and paints it. The production path sets the viewport and swaps the HWND-backed GL context. The offscreen path renders into a pooled GL texture, calls 'glReadPixels' into 'ShareableBitmap', and sends a bitmap handle back. The existing 'remoteContextHostedIdentifier' branch also paints to a pooled GL texture, but that texture is for WebKit remote-frame hosting and is not the WinUI browser surface.

That distinction matters: enabling WebKit's offscreen flag would reintroduce CPU readback, and reusing the remote-frame-hosted texture would not connect the browser's page to WinUI. Neither is an acceptable implementation of this feature.

### Implemented experimental pieces

- Versioned descriptor ABI: 'experiments/wincairo-shared-d3d-composition/SharedSurfaceProtocol.h'.
- Bounded frame scheduler: two/three slots, monotonic frame IDs, resize generations, backpressure and stale-frame rejection.
- Windows CI tests for descriptor/ring invariants.
- D3D11 diagnostic that allocates three BGRA8 keyed-mutex textures, opens them on a second same-adapter device, performs repeated producer/consumer GPU copies, and recreates the ring at a new size.
- Separate ANGLE EGL import/render and DirectComposition diagnostics.

These validate device/driver capabilities and resource synchronization. They do not carry the actual page output.

### Required production changes

1. Add a real native output-surface owner to the browser host. It must create a bounded D3D11 shared-texture ring and a composition swap chain, expose resize generations and frame sequence IDs, and keep all handles alive until the GPU process acknowledges retirement.
2. Extend the pinned WebKit UIProcess → WebProcess → GPUProcess contract. The descriptor must be sent to the correct page and forwarded through 'CreateWCLayerTreeHost', including after provisional/replacement WebProcesses. Resize and teardown need explicit generation/acknowledgement handling; never treat a raw legacy shared handle as an NT handle or close it with 'CloseHandle'.
3. Add a GPU-process ANGLE import object that creates an EGL surface from each shared D3D11 texture using the verified ANGLE D3D texture-share-handle extension. It must verify adapter LUID/format/dimensions, coordinate keyed-mutex ownership, and recreate resources on device loss.
4. Change 'WCSceneContext'/TextureMapper's final render target so 'WCScene::update' paints directly into the imported shared surface. Do not use 'ShareableBitmap', 'glReadPixels', or CPU bitmap IPC in this path.
5. Add the WinUI 'SwapChainPanel' interop surface using the supported 'ISwapChainPanelNative' contract, then present only completed frames without blocking the UI thread. Keep the existing HWND path as fallback until the end-to-end page tests pass.
6. Validate provisional navigation, replacement WebProcesses, resizing/DPI, multiple tabs, popups, dialogs, occlusion, keyed-mutex timeout, device removal, and frame latency/GPU utilization.

The descriptor and diagnostics are deliberately not wired into production yet: no shared page-output handle is currently transported through WebKit IPC, no 'WCScene' EGL import is connected to the descriptor, and no WinUI 'SwapChainPanel' receives live page frames. The HWND renderer remains the only production path. Do not advertise or enable the composition renderer until the full pipeline and its tests pass.
