# WinCairo GPU-to-WinUI composition prototype

Status: P1.5 ANGLE import/render plus DirectComposition GPU presentation passed on the hosted Windows runner (Blaze 1.1.40 / Actions run 206). The test validates keyed-mutex ownership across the ANGLE producer, a second D3D11 device, and a DirectComposition swap-chain consumer, including a GPU-only copy before Present. It remains a diagnostic harness: actual WebKit WCScene output transport and WinUI composition integration are not implemented. The stable HWND renderer remains the only production renderer.

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

- The browser/UI process owns a D3D11 device compatible with the WinUI composition device and creates a small ring (2–3) of BGRA8 shared textures with legacy DXGI handles and keyed-mutex synchronization.
- Transport the shared-resource identifier through an explicit WebKit IPC message. The current ANGLE import uses a legacy DXGI share-handle token; do not pass it through DuplicateHandle or CloseHandle as though it were an NT HANDLE. If switching to NT handles, verify the pinned ANGLE import path supports OpenSharedResource1.
- The GPU process opens the shared resources on the same adapter. ANGLE's EGL D3D texture-client-buffer/share-handle interop is the first candidate for exposing the texture to WebKit's GL context. Verify the exact ANGLE extensions and adapter/device compatibility at runtime; do not assume an EGL extension is present just because ANGLE is bundled.
- `WCScene` renders the root layer into the imported texture/FBO, then signals frame readiness. No `glReadPixels`, CPU staging texture, or bitmap IPC is allowed on this path.
- The UI process copies or binds the completed shared texture into a `IDXGISwapChain1` created with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` and `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`, using `CreateSwapChainForComposition`. Attach it through the supported WinUI `SwapChainPanel` interop or a supported composition visual bridge.
- Use an explicit frame sequence number and keyed-mutex/fence handoff. Never block the UI thread waiting for the GPU process. Drop stale frames rather than queueing unbounded latency.

### Important design checks

- The pinned ANGLE runtime exposes D3D texture-client-buffer, D3D shared-handle client-buffer, and keyed-mutex/query-surface-pointer extensions. The 1.1.39 diagnostic validates one cross-device legacy DXGI shared-handle import with an explicit EGL keyed-mutex handoff. A multi-frame ring and WebKit IPC remain required before production composition.
- Cross-process D3D resource sharing requires compatible adapter/device configuration and correct handle duplication/security.
- Avoid relying on a legacy shared-handle API if the pinned ANGLE import path only supports it; compare legacy-handle support and keyed mutex/fence requirements.
- A direct composition swap chain is preferable to copying into a second texture if WinUI can consume the same device/resource. If the UI and GPU process devices cannot share a swap chain, use a shared texture ring and one GPU-side copy, never CPU readback.
- Do not call `eglSwapBuffers` on a dummy HWND and assume it updates the WinUI visual. The scene must render into the exported resource itself.

## Implemented P0/P1 diagnostics

`CompositionProbe.cpp` is a standalone Windows x64 diagnostic built by the experimental browser workflow. P0 validates D3D11 resource transport. P1 now exercises the pinned ANGLE runtime itself:

- Reports ANGLE EGL vendor/version, display/device extensions and the ANGLE D3D11 adapter LUID when `libEGL.dll` can initialize a D3D11 display.
- Creates a BGRA8 D3D11 texture using a legacy DXGI shared handle.
- Transfers the legacy DXGI share-handle token to a child process through an inherited anonymous pipe; the child opens the resource and verifies its pixels.
- Opens the texture on a second D3D11 device on the same adapter and verifies a known pixel pattern.
- Queries the D3D11 device actually owned by the initialized ANGLE EGL display.
- Creates a BGRA8 legacy-shared D3D11 texture on a second device on the same adapter, so ANGLE must open a real cross-device shared resource rather than receive a same-device texture pointer.
- Creates a legacy DXGI shared handle from the producer texture and imports it with `eglCreatePbufferFromClientBuffer(EGL_D3D_TEXTURE_2D_SHARE_HANDLE_ANGLE)`, exercising ANGLE's `OpenSharedResource` / `OpenSharedResource1` path.
- Requires `EGL_BIND_TO_TEXTURE_RGBA` and explicitly sets `EGL_TEXTURE_TARGET=EGL_TEXTURE_2D` plus `EGL_TEXTURE_FORMAT=EGL_TEXTURE_RGBA`, matching ANGLE's documented DirectX interop setup.
- First renders a known clear color to a normal EGL pbuffer as a control, then renders to the imported legacy DXGI shared-handle pbuffer.
- Checks framebuffer completeness, GL errors and GLES pixel readback for each surface, calls `eglSwapBuffers`, and unbinds the EGL surface.
- Reads the texture back through a D3D11 staging resource and logs the resulting BGRA pixel values. This readback is diagnostic-only and is not used by the proposed renderer.

The hosted-runner P1 test verifies ANGLE import/render and producer-device BGRA readback. P1.5 presents that texture through a temporary HWND, `CreateSwapChainForComposition`, a DirectComposition visual, and a GPU-only copy/Present operation. The 1.1.39 iteration sets `D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX`, hands key 0→1 to ANGLE, queries `EGL_DXGI_KEYED_MUTEX_ANGLE` (`0x33A2`), acquires key 1 for GLES rendering, releases key 2 after `glFinish`, and requires the producer to acquire key 2 before readback and release key 3. The 1.1.40 iteration extends the same ownership chain through DirectComposition: acquire key 3 for the GPU copy, then release key 4 before Present. The hosted result is pending.
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

### P1.5 — DirectComposition presentation (new diagnostic)
- Create a D3D11 composition swap chain and attach it to a DirectComposition visual on a temporary HWND.
- Copy the ANGLE-rendered shared texture into the swap-chain back buffer on the GPU and call Present/Commit.
- Keep this test separate from WinUI; it validates DComp primitives, not the WinUI composition bridge.
- Add an opt-in WebKit port setting for a composition output handle.
- Replace the HWND-specific `WCSceneContext` construction with a surface-provider interface.
- Keep the current HWND EGL surface as the default/fallback.
- Make the TextureMapper render to an FBO backed by the imported shared texture.
- Send frame-ready notifications with bounded in-flight frames and explicit resize generations.

### P2 — WinUI presentation
- The DirectComposition HWND probe is not yet attached to a WinUI `SwapChainPanel`; this remains a separate integration milestone.
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


## Pinned WebKit WCScene integration findings

The pinned WebKit source `d641dd75af65b0469eff30bb76a97e01e02633fc` was inspected directly:
- `WCScene::update` calls `TextureMapper::beginPainting`, paints the layer tree, and either calls `WCSceneContext::swapBuffers()` to present to the native HWND or, when `m_usesOffscreenRendering` is true, performs `glReadPixels(GL_BGRA, GL_UNSIGNED_BYTE)` into a `ShareableBitmap`.
- `WCSceneContext` currently creates its `GLContext` from the native HWND, then delegates to `GLContext::swapBuffers()`.
- The existing offscreen mode is a CPU readback path and is not suitable as the requested zero-copy WinUI renderer.

The next engine change must replace the HWND-bound scene surface with a renderable D3D11 shared-texture pbuffer, add explicit GPU-process/UI-process texture ownership and synchronization, and present the shared texture through a DirectComposition/WinUI surface. It must preserve the current HWND renderer as fallback and validate resize, DPI, device loss, and multiple tabs before activation.

## Exact pinned WebKit IPC and context seam

The pinned source was traced through the real scene-creation path:

1. `Source/WebKit/WebProcess/GPU/graphics/wc/RemoteWCLayerTreeHostProxy.cpp` sends `GPUConnectionToWebProcess::CreateWCLayerTreeHost(identifier, m_page->nativeWindowHandle(), m_usesOffscreenRendering)`.
2. `Source/WebKit/GPUProcess/GPUConnectionToWebProcess.messages.in` declares that IPC message; `GPUConnectionToWebProcess.cpp` dispatches it to `RemoteWCLayerTreeHost::create`.
3. `RemoteWCLayerTreeHost.cpp` obtains a `WCSharedSceneContextHolder` keyed by the native HWND, constructs `WCSceneContext(nativeWindow)` on the graphics work queue, and initializes `WCScene`.
4. `WCSceneContext.cpp` currently calls `WebCore::GLContext::create(PlatformDisplay::sharedDisplay(), reinterpret_cast<GLNativeWindowType>(nativeWindow))`; its `swapBuffers()` is just `m_glContext->swapBuffers()`.
5. `WCScene.cpp` paints the TextureMapper layer tree. The normal path calls `m_context->swapBuffers()`. The separate `usesOffscreenRendering` path allocates `ShareableBitmap`, calls `glReadPixels`, and returns the bitmap handle in `UpdateInfo`.

This confirms that the narrow source-level extension point is the `CreateWCLayerTreeHost` descriptor plus `WCSharedSceneContextHolder` / `WCSceneContext`, not the WinUI `MainPage` or the existing WebKit HWND subclass. The GPU-process renderer must receive a versioned per-window/per-view output-surface descriptor, create or import its ANGLE EGL surface from the shared D3D11 resource, and make `WCScene` render to that surface. Frame-ready sequence numbers and keyed-mutex/fence ownership must travel with the descriptor/updates; `swapBuffers()` must not fall back to `glReadPixels` or bitmap IPC.

Before implementing the patch, resolve the context-sharing model explicitly: WebKit currently shares one `WCSceneContext` for all layer-tree hosts using the same HWND (including a provisional WebProcess during navigation). A replacement must preserve that lifecycle while preventing one tab/view from releasing or overwriting another view's surface. Resize generations, device loss, process restart, and GPU resource retirement must be represented in the protocol. The current diagnostic's passing ANGLE/DComp test is the prerequisite proof for the graphics primitives, not proof that this IPC/context work is complete.
