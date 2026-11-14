# WebKitHost native bridge

This DLL hosts the real WinCairo `WKView` child window. It is not a browser-engine abstraction and it has no WebView2 fallback.

Configure with a built WebKit Windows port:

```powershell
cmake -S Native/WebKitHost -B Native/WebKitHost/build -A x64 -DWEBKIT_ROOT=C:/src/WebKit/WebKitBuild/Release
cmake --build Native/WebKitHost/build --config Release
```

The WebKit build must expose `include/WebKit/WKView.h` and the WebKit2 import library. If the pinned upstream build layout differs, update the CMake hints based on the actual build products; do not silently link a different engine.


## Experimental shared D3D surface owner

The native host now exposes an opt-in C ABI for creating a per-view, three-slot
D3D11 shared-texture ring and copying its fixed-width descriptor. It selects the
primary DXGI adapter, creates BGRA8 render-target/shareable textures with keyed
mutexes, and retains every texture/device until the owner is destroyed.

- `WB_GpuSurfaceCreate` creates the resource generation.
- `WB_GpuSurfaceGetDescriptor` copies the versioned descriptor to the caller.
- `WB_GpuSurfaceDestroy` releases resources; callers must first drain GPU-process
  ownership and wait for a retirement acknowledgement.

This is the browser-process resource-owner increment only. The exports are not
yet called by the WinUI page host, and WebKit IPC, GPU-process ANGLE import,
TextureMapper targeting, and SwapChainPanel presentation remain future integration
steps. The existing HWND renderer remains the live browser renderer.
