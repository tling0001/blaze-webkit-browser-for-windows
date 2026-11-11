# WebKitHost native bridge

This DLL hosts the real WinCairo `WKView` child window. It is not a browser-engine abstraction and it has no WebView2 fallback.

Configure with a built WebKit Windows port:

```powershell
cmake -S Native/WebKitHost -B Native/WebKitHost/build -A x64 -DWEBKIT_ROOT=C:/src/WebKit/WebKitBuild/Release
cmake --build Native/WebKitHost/build --config Release
```

The WebKit build must expose `include/WebKit/WKView.h` and the WebKit2 import library. If the pinned upstream build layout differs, update the CMake hints based on the actual build products; do not silently link a different engine.
