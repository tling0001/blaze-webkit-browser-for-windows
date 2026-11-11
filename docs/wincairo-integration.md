# WinCairo integration notes

## Verified public Windows API

The official WebKit Windows port exports the C API in `Source/WebKit/UIProcess/API/C/win/WKView.h`:

- `WKViewCreate(RECT, WKPageConfigurationRef, HWND)`
- `WKViewGetWindow(WKViewRef)`
- `WKViewGetPage(WKViewRef)`
- `WKViewSetParentWindow(WKViewRef, HWND)`
- `WKViewSetIsInWindow(WKViewRef, bool)`
- `WKViewWindowAncestryDidChange(WKViewRef)`
- `WKViewSetUsesOffscreenRendering(WKViewRef, bool)`

WebKit's own Windows MiniBrowser constructs a `WKWebsiteDataStore`, `WKContext`, `WKPreferences`, and `WKPageConfiguration`, creates the native view with `WKViewCreate`, and navigates with `WKPageLoadURL`. It uses `WKPageGoBack`, `WKPageGoForward`, and `WKPageReload` for navigation. This is the integration pattern used here.

## Host boundary

The WinUI app owns the top-level window and browser chrome. A native C++ DLL owns each WebKit view and its child HWND. The C# side positions that child window over the WinUI browser content rectangle using physical pixels. The engine never uses WebView2.

The C ABI is deliberately narrow. Do not pass C++ STL, WebKit ref types, or exceptions across the managed boundary. Every returned native view handle must be destroyed exactly once. All WK API calls and HWND layout work must be serialized on the owning UI thread.

## Persistent data and security

The bridge creates a website data store using WebKit's C API. Before shipping, verify persistence location, private browsing isolation, cookie and website-data clearing, TLS trust handling, popup policy, download destinations, permission requests, and process termination/recovery against the exact pinned engine build.

Never auto-accept TLS certificate errors. Never expose private WebKit APIs as a substitute for a public API without documenting and testing the compatibility risk.

## Engine build

The official Windows-port guide documents a 64-bit build using Visual Studio's Desktop development with C++, CMake, Ninja, LLVM/clang-cl, Perl, Python 3.11, Ruby, and gperf. It builds with `perl Tools/Scripts/build-webkit --release` and runs `WebKitBuild/Release/bin64/MiniBrowser.exe`.

The engine workflow pins the WebKit Git revision and preserves the build output plus its matching WebKitRequirements DLLs. A build is not distributable until a clean Windows runner can launch the app and load a remote HTTPS page with the packaged runtime alone.

## References

- [WebKit Windows port build guide](https://docs.webkit.org/Ports/WindowsPort.html)
- [WebKit Windows WKView header](https://github.com/WebKit/WebKit/blob/main/Source/WebKit/UIProcess/API/C/win/WKView.h)
- [WebKit Windows MiniBrowser host implementation](https://github.com/WebKit/WebKit/blob/main/Tools/MiniBrowser/win/WebKitBrowserWindow.cpp)
- [WebKit Windows port source list](https://github.com/WebKit/WebKit/blob/main/Source/WebKit/PlatformWin.cmake)
