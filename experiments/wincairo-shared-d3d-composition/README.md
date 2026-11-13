# WinCairo GPU-to-WinUI composition prototype

Status: P1 ANGLE texture-import/render probe implemented; WebKit WCScene output integration and WinUI presentation remain unimplemented.

## Current pinned rendering path

The pinned WebKit revision is `d641dd75af65b0469eff30bb76a97e01e02633fc`. Its `USE(GRAPHICS_LAYER_WC)` path is already GPU accelerated:

1. `RemoteWCLayerTreeHost` creates the GPU-process scene on the remote graphics stream work queue.
2. `WCSceneContext` constructs `WebCore::GLContext` from a native HWND.
3. `WCScene::update` applies the layer tree and paints it through `TextureMapper`.
4. Normal presentation calls `WCSceneContext::swapBuffers()`.
5. `WKViewSetUsesOffscreenRendering` switches to a separate bitmap path; `WCScene::update` calls `glReadPixels` and transfers a `ShareableBitmap` through IPC.

This means the correct seam is **WCSceneContext's presentation target**, not the WinUI host's HWND parenting. Replacing the current host window with a composition API alone does not change the EGL surface created by WebKit.

## Candidate zero-copy bridge

Use a D3D11 shared texture as the process boundary and a WinUI composition swap chain as the presentation boundary.

- The browser/UI process owns a D3D11 device compatible with the WinUI composition device and creates a small ring (2–3) of BGRA8 shared textures with NT handles and keyed-mutex synchronization.
- A broker duplicates the NT handles into the WebKit GPU process. Handle lifetime is explicit; never serialize raw process-local HANDLE values as if they were globally valid.
- The GPU process opens the shared resources on the same adapter. ANGLE's EGL D3D texture-client-buffer/share-handle interop is the first candidate for exposing the texture to WebKit's GL context. Verify the exact ANGLE extensions and adapter/device compatibility at runtime; do not assume an EGL extension is present just because ANGLE is bundled.
- `WCScene` renders the root layer into the imported texture/FBO, then signals frame readiness. No `glReadPixels`, CPU staging texture, or bitmap IPC is allowed on this path.
- The UI process copies or binds the completed shared texture into a `IDXGISwapChain1` created with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` and `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`, using `CreateSwapChainForComposition`. Attach it through the supported WinUI `SwapChainPanel` interop or a supported composition visual bridge.
- Use an explicit frame sequence number and keyed-mutex/fence handoff. Never block the UI thread waiting for the GPU process. Drop stale frames rather than queueing unbounded latency.

### Important design checks

- The pinned ANGLE runtime exposes both D3D texture-client-buffer and D3D shared-handle client-buffer extensions; the P1 probe validates the latter using an NT shared handle from a second device on the same adapter.
- Cross-process D3D resource sharing requires compatible adapter/device configuration and correct handle duplication/security.
- Avoid relying on a legacy shared-handle API if the pinned ANGLE import path only supports it; compare NT-handle support and keyed mutex/fence requirements.
- A direct composition swap chain is preferable to copying into a second texture if WinUI can consume the same device/resource. If the UI and GPU process devices cannot share a swap chain, use a shared texture ring and one GPU-side copy, never CPU readback.
- Do not call `eglSwapBuffers` on a dummy HWND and assume it updates the WinUI visual. The scene must render into the exported resource itself.

## Implemented P0/P1 diagnostics

`CompositionProbe.cpp` is a standalone Windows x64 diagnostic built by the experimental browser workflow. P0 validates D3D11 resource transport. P1 now exercises the pinned ANGLE runtime itself:

- Reports ANGLE EGL vendor/version, display/device extensions and the ANGLE D3D11 adapter LUID when `libEGL.dll` can initialize a D3D11 display.
- Creates a BGRA8 D3D11 texture using an NT shared handle and keyed mutex.
- Duplicates the shared handle into a child process, transferring the child-local handle value through an inherited anonymous pipe (not by assuming HANDLE values are global).
- Opens the texture on a second D3D11 device on the same adapter, synchronizes with the keyed mutex, and verifies a known pixel pattern.
- Queries the D3D11 device actually owned by the initialized ANGLE EGL display.
- Creates a BGRA8 NT-shared D3D11 texture on a second device on the same adapter, so ANGLE must open a real cross-device shared resource rather than receive a same-device texture pointer.
- Creates an NT shared handle from the producer texture and imports it with `eglCreatePbufferFromClientBuffer(EGL_D3D_TEXTURE_2D_SHARE_HANDLE_ANGLE)`, exercising ANGLE's `OpenSharedResource` / `OpenSharedResource1` path.
- Creates an OpenGL ES 2 context, clears the imported pbuffer to a known RGBA color, calls `glFinish`, and unbinds the EGL surface.
- Reads the texture back through a D3D11 staging resource and verifies the resulting BGRA pixel values. This readback is diagnostic-only and is not used by the proposed renderer.

The test fails the workflow if the staged ANGLE runtime is missing, cannot initialize, does not expose its D3D11 device, cannot import the texture, or produces incorrect pixels. It still does **not** modify WebKit `WCSceneContext` or present actual WebKit page frames through WinUI.
## Prototype milestones

### P0 — capability probe (no visible product change)
- Record EGL vendor/version/extensions and D3D adapter LUID in the WebKit GPU process.
- Probe the exact ANGLE extensions needed to import an externally created D3D11 texture.
- Create/open a shared D3D11 texture across the browser and GPU processes.
- Render a known GPU test pattern into it and verify the UI process can consume it.
- Fail closed to the current HWND renderer if any capability is missing.

### P1 — ANGLE external texture import (implemented diagnostic)
- Import a D3D11 texture created on ANGLE's own device into an EGL pbuffer.
- Render a known GLES pattern and verify the D3D11 resource after GPU completion.
- Keep the staging readback confined to the test harness.

### P2 — WebKit scene output adapter
- Add an opt-in WebKit port setting for a composition output handle.
- Replace the HWND-specific `WCSceneContext` construction with a surface-provider interface.
- Keep the current HWND EGL surface as the default/fallback.
- Make the TextureMapper render to an FBO backed by the imported shared texture.
- Send frame-ready notifications with bounded in-flight frames and explicit resize generations.

### P2 — WinUI presentation
- Add a WinUI `SwapChainPanel` in the existing browser content slot.
- Attach the composition swap chain through the documented WinUI interop interface.
- Keep all navigation, keyboard, mouse, touch, IME, accessibility, and context-menu routing in WebKit; the surface bridge must only transport pixels and frame synchronization.
- Retire the owned popup HWND only after parity tests pass. Keep popup windows for actual WebKit popup/auxiliary surfaces until their own composition surfaces are implemented.

### P3 — hardening and measurements
- Exercise 100%, 125%, 150%, and 200% DPI; mixed-DPI monitor moves; live resize; minimize/restore; display hot-plug; HDR/SDR; GPU reset/device removed.
- Test pointer, wheel, touch, pen, keyboard, IME composition, text selection, drag/drop, focus, caret, and accessibility tree continuity.
- Test JavaScript alert/confirm/prompt, before-unload, file chooser, permission prompts, context menus, downloads, and popup windows while the page is visible.
- Test 1, 5, and 20 tabs, background throttling, tab switches, navigation, and process crashes/restarts.
- Measure GPU frame time, UI presentation latency, dropped frames, CPU usage, GPU copy bandwidth, memory use, and p50/p95/p99 input-to-photon latency. Compare to the current HWND path on the same hardware.
- Require no CPU pixel readback and no unbounded frame queue. Keep feature opt-in until all tests pass.

## Benchmark instrumentation

Use QPC timestamps at (a) WCScene frame submission, (b) GPU completion/fence signal, (c) UI frame acquisition, and (d) composition present. Correlate with a monotonically increasing frame ID. Emit ETW events under a Blaze provider and capture GPUView/WPA traces. Report warm-up separately and collect at least 10,000 frames per scenario. Do not infer input-to-photon latency from FPS alone.

## Acceptance gate

The experiment is not ready for default use until it has:
- Verified ANGLE/D3D shared-resource interoperability on the pinned SDK.
- A real rendered WebKit page visible in a WinUI composition surface.
- No `glReadPixels` or CPU bitmap transfer on the experimental path.
- Correct resize/DPI/input/modal/popup/multi-tab behavior.
- Stable GPU resource lifetime across tab close, navigation, process restart, and device removal.
- Measured latency/CPU/GPU results showing a meaningful improvement over the HWND baseline.

Until then, Blaze continues to use the tested WinCairo HWND renderer. This document is a plan for a measurable prototype, not a claim that a shared-surface renderer is implemented.
