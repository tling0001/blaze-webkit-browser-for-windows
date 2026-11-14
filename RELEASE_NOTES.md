# Blaze release notes

## [1.1.44] - 2026-11-13

### GPU shared-surface protocol groundwork
- Add a fixed-width, versioned output-surface descriptor for the future browser-process/WebKit GPU-process boundary, including per-view identity, resize generations, BGRA8 format, adapter identity, bounded shared-resource slots, and keyed-mutex synchronization metadata.
- Add a bounded frame-ring state machine with monotonic frame IDs, immediate backpressure, newest-frame presentation selection, stale-frame dropping, and rejection of tickets from replaced resize generations.
- Add deterministic CI tests for descriptor validation, bounded queue behavior, stale-frame rejection, and resize-generation lifetime.
- Keep the existing WinCairo HWND renderer unchanged. This protocol is not yet transported through WebKit IPC and does not yet render live page frames into a WinUI composition surface.

### Versioning
- Align product, assembly, file, informational and package versions at 1.1.44.

### Validation
- CI now builds and runs the standalone shared-surface protocol tests alongside the ANGLE/D3D diagnostic. Live WebKit frame transport and WinUI presentation remain unimplemented.

## [1.1.43] - 2026-11-13

### Native password manager foundation
- Add a native WinUI Passwords settings tab for saving, reviewing, revealing on explicit request, and deleting website credentials.
- Store secrets in the current Windows user's Credential Locker instead of Blaze's JSON profile files.
- Normalize saved website resources to HTTP(S) origins and replace duplicate username entries for the same origin.
- Require confirmation before deleting a saved credential; keep passwords masked until the user explicitly reveals them.
- Automatic save prompts and automatic form filling are not yet connected: those require a dedicated, origin-validated WebKit page-script bridge and remain disabled rather than relying on unsafe URL or prompt hacks.

### GPU composition status
- Keep the existing WinCairo GPU HWND renderer as the production fallback.
- The shared-D3D-to-WinUI SwapChainPanel path is not enabled: the WinUI surface, cross-process output descriptor, WCScene ANGLE import, frame synchronization and device-loss recovery still need to land and pass live-page tests. The standalone ANGLE probe is not evidence of completed browser-frame transport.

### Versioning
- Align product, assembly, file, informational and package versions at 1.1.43.

### Validation
- Pending main-branch WinUI API discovery, Release compilation, installer integration, and password-manager UI/security smoke coverage.

## [1.1.42] - 2026-11-14

### GPU-accelerated WinCairo rendering
- Explicitly keep live WKViews on the normal WinCairo `WCScene` / TextureMapper GPU presentation path by calling `WKViewSetUsesOffscreenRendering(false)`.
- Disable the separate offscreen `ShareableBitmap` / `glReadPixels` CPU-readback path for production browser pages.
- Emit a renderer diagnostic when a live page is created. The current production renderer remains the native WinCairo GPU surface; shared-D3D texture transport into a WinUI composition surface remains a separate future integration, not something this release claims to have completed.

### Keyboard and window activation
- Restore the WinUI owner as foreground without calling `SetActiveWindow`, which was stealing keyboard focus from WebKit.
- Preserve WebKit child focus after foreground restoration only when the child still owns focus; do not override a user's newer focus on the address bar or another WinUI control.
- Add a real Unicode typing smoke test against an HTML input after a WebKit click and WinUI/Mica activation restoration.

### Workflow and release controls
- Add a manual Actions checkbox, `publish_release_on_smoke_failure`, to explicitly permit publishing the installer when the browser smoke test fails.
- The override is limited to manual workflow dispatch and only applies when the ANGLE GPU capability probe and installer artifact upload both succeed. Other build/package failures still block publication.
- Keep ordinary push builds strict: a failed smoke test continues to fail the build and does not publish a release unless the explicit manual override was selected.

### Versioning
- Align product, assembly, file, informational and package versions at 1.1.42.

### Validation
- Pending main-branch Release build, installer integration, Win32 focus/keyboard typing test, and ANGLE GPU capability probe.

## [1.1.41] - 2026-11-14

### Win32 window activation and Mica
- Defer restoration of the WinUI owner activation until after the native WebKit host's `WM_MOUSEACTIVATE` transaction has returned. This avoids racing Windows' activation processing while preserving focus transfer to the WebKit child.
- Restore activation on the WinUI owner's message queue and emit focused HWND/foreground diagnostics for failed activation investigations.
- Update the Release smoke test to wait for the deferred owner-activation message before asserting that the WinUI HWND is foreground; the assertion remains strict.

### GPU composition groundwork
- Keep the ANGLE shared-D3D11 texture and DirectComposition GPU-copy probe in the mainline as diagnostic-only infrastructure.
- Hosted Actions verified the keyed-mutex ANGLE import/render and GPU-only DirectComposition copy path. This does not yet constitute WebKit `WCScene` integration or a production renderer.

### General app and build
- Continue using the stable WinCairo HWND renderer by default; no CPU bitmap readback fallback is presented as GPU composition.
- Bump product, assembly, file, informational and package versions together.

### Validation
- Pending the new main-branch Release build, installer integration, and deferred Win32 activation/Mica smoke test.

## [1.1.40] - 2026-11-13

### Experimental GPU renderer
- Extend keyed-mutex ownership through the DirectComposition consumer: producer readback releases key 3, DirectComposition acquires key 3 for the GPU-only swap-chain copy, and releases key 4 before Present.
- Require the DComp consumer to acquire the shared texture before copying; no unsynchronized read of the ANGLE-rendered resource is accepted.

### Native WebKit activation
- Restore the WinUI owner as foreground immediately after transferring focus to the WebKit HWND, preserving keyboard-focus intent while testing whether Mica remains active.
- Expand smoke-test failure diagnostics to report the foreground HWND class, title, process, and owner.

### Build quality
- Make address suggestion items observable and restore explicit OneWay bindings, satisfying the WinUI analyzer without WMC1506 warnings.

### Validation
- Pending hosted Release build, warning scan, full keyed-mutex EGL/DComp probe, installer, and native-focus smoke test.

## [1.1.39] - 2026-11-13

### Experimental GPU renderer
- Require keyed-mutex-capable D3D11 shared textures in the ANGLE import diagnostic.
- Synchronize producer initialization, ANGLE surface rendering, and producer readback with explicit keys 0 → 1 → 2 → 3.
- Query the imported EGL surface's `EGL_DXGI_KEYED_MUTEX_ANGLE` pointer and acquire/release it around GLES rendering; no unsynchronized producer readback is accepted.
- Keep DirectComposition presentation GPU-only. This still does not connect real WebKit `WCScene` frames to WinUI; versioned WebKit SDK source changes, IPC frame transport, and a WinUI composition bridge remain the next milestone.

### Win32 WebKit activation
- Mark the owned native host as no-activate and subclass the actual WKView HWND to return `MA_NOACTIVATE` on mouse activation while transferring keyboard focus to WebKit.
- Preserve the WinUI owner as the foreground top-level window so Mica and title-bar active state remain consistent when clicking page content.

### Build quality
- Remove incorrect `OneWay` bindings from immutable address-suggestion template records, eliminating the three WMC1506 XAML warnings without suppressing diagnostics.

### Validation
- Pending hosted Release build, warning scan, keyed-mutex EGL import/render, DirectComposition presentation, installer, and native-focus smoke test.

## [1.1.38] - 2026-11-13

### Native WebKit focus and tab automation
- Extend Release smoke coverage to click the native WebKit surface and assert the WinUI main HWND remains foreground, protecting the active Mica/title-bar state.
- Focus the XAML address control before keyboard tab accelerators in smoke tests, and inspect the UIA tree when the new-tab URL is not reflected.
- Keep close-during-navigation and close-after-load checks strict; do not silently skip failed tab creation.

### GPU experiment documentation
- Correct the cross-process shared-handle description and explicitly retain keyed-mutex synchronization as the next GPU correctness gate.
- Keep the successful ANGLE/D3D11 and DirectComposition HWND probe separate from the still-unimplemented WinUI SwapChainPanel and WebKit WCScene bridge.

### Validation
- Pending GitHub Actions Release build, warning scan, installer integration, native-focus smoke test, and internal-page/tab smoke tests.

## [1.1.37] - 2026-11-13

### Internal pages and tab behavior
- Add smoke assertions for the History and Settings internal URLs and verify both pages open in their own tabs from a WebKit page.
- Verify entering a web address from Settings replaces the internal surface with a live WebKit view in that same tab.
- Disable bookmarking for private `blaze://` pages.

### Warning cleanup and test targeting
- Resolve the final nullable warning in the saved-search suggestion title.
- Target every post-discovery WinApp UI operation at the exact WinUI main HWND. Keep process targeting only for initial window discovery, avoiding the second owned WebKit HWND selection ambiguity.

### DirectComposition GPU presentation experiment
- Extend the ANGLE shared-texture probe to create a DirectComposition swap chain and visual on a temporary HWND.
- Copy the ANGLE-rendered D3D11 texture to the composition swap-chain back buffer on the GPU, then Present and Commit without CPU pixel upload.
- Link the experimental probe against `dcomp.lib` and document the scope: this validates DComp presentation primitives, not yet the WinUI integration or WebKit WCScene bridge.

### Validation
- Pending GitHub Actions Release build, warning scan, ANGLE/DComp probe, installer integration, and expanded internal-page/WebKit smoke tests.

## [1.1.36] - 2026-11-13

### Internal browser pages and tab navigation
- Open History and Settings in their own browser tab whenever the active tab contains a website; reuse the native start tab when it is already selected.
- Give the pages stable `blaze://history` and `blaze://settings` address-bar URLs, retain their page content when switching tabs, and turn the same tab back into a normal WebKit tab as soon as a web address or search is submitted.
- Recreate private internal-page URLs as a safe start tab after restart rather than passing the private scheme to WebKit.

### Native window activation
- Prevent the owned Win32 WebKit surface host from activating when clicked, preserving the WinUI top-level window's active state and Mica/title-bar appearance.
- Direct keyboard focus to the WebKit child HWND while allowing mouse input to reach the page.

### Build and smoke-test quality
- Fix all known WinUI WUI2011 binding warnings by declaring the address-suggestion template bindings as OneWay.
- Fix the nullable search-term warning and prevent duplicate direct-search suggestion entries.
- Scope post-launch UI automation to the actual WinUI main HWND, avoiding accidental selection of the owned WebKit popup window.

### Experimental GPU renderer
- Keep the experimental ANGLE import/render probe and production WinCairo HWND renderer separate; the latter remains the fallback until the WebKit WCScene texture bridge is validated.
- Continue the synchronized shared-texture and DirectComposition experiments before exposing an experimental renderer switch.

### Validation
- Pending GitHub Actions Release build, native-host compile, ANGLE probe, installer integration and WebKit tab-close smoke test.

## [1.1.35] - 2026-11-13

### Address-bar suggestions
- Add Chrome-style inline suggestions from local browsing history, bookmarks, and previously used search queries.
- Rank repeated and recent visits above less relevant matches, show the source/URL beneath each result, and include a direct search action using the selected search engine.
- Keep suggestions local to the device; do not send every typed character to a remote suggestion provider.

### Native tab-close stability
- Hide retired WebKit views and detach their callbacks immediately, but retain the WKView and its native resources until the browser page shuts down.
- Remove the 750 ms delayed WKRelease path, which still allowed the GPU process and WebKit compositor to have outstanding work when the UI process destroyed the view.
- Add native retirement markers and keep shutdown-only finalization explicit. This is a bounded-lifetime diagnostic mitigation, not yet proof that every WebKit close race is resolved.

### ANGLE probe corrections
- Fix the cross-process shared-handle byte-count check to use the actual legacy handle token string.
- Remove keyed-mutex operations from a texture created with D3D11_RESOURCE_MISC_SHARED; keyed mutex APIs are only valid for textures created with the keyed-mutex flag.
- Flush the producer D3D11 context before staging readback.
- Correct the probe success wording to identify the legacy DXGI handle path.

### Release smoke test
- Use the UI automation target selector to focus AddressBox instead of the standalone focus command, which intermittently failed after selecting a newly created TabView tab.
- Extend the post-close process-survival window to two seconds for the loading-tab stress loop.

### Validation
- Run 200 compiled the WinUI app, published the self-contained browser, built the installer, and passed all seven installed-app integration checks.
- The ANGLE probe did not execute because the probe source failed to compile (stale `remoteHandleText` and removed keyed-mutex references). The Release smoke test passed eight checks but failed the standalone AddressBox focus action before reaching the tab-close loops.
- The actual WebKit `WCScene` implementation was inspected at the pinned SDK revision. It currently either renders directly to its native HWND or uses `glReadPixels` into a ShareableBitmap for offscreen rendering; neither mode exposes a GPU texture to WinUI. The D3D composition connection therefore still requires a WebKit SDK source change and an end-to-end GPU-process/UI-process texture bridge. No CPU-readback fallback is being misrepresented as GPU composition.

## [1.1.34] - 2026-11-12

### ANGLE shared-texture interop
- Use legacy DXGI shared handles from IDXGIResource::GetSharedHandle for EGL_ANGLE_d3d_share_handle_client_buffer. The pinned ANGLE implementation opens these through ID3D11Device::OpenSharedResource; NT handles from CreateSharedHandle require OpenSharedResource1 and left the imported EGL surface transparent.
- Remove the keyed mutex from the first transport test so it isolates handle import and GPU visibility; flush the producer and ANGLE contexts before readback.
- Pass the legacy DXGI share-handle token through the child-process pipe without DuplicateHandle or CloseHandle, which are invalid for this non-NT handle type.

### Tab-close diagnostics
- Avoid calling WKPageStopLoading synchronously in the tab-close event; keep the native view retained during the quiescence window and let WKRelease perform final page shutdown.
- Add managed lifecycle markers around native retirement, view-model removal and remaining-view layout.
- Extend close stress-test waits beyond the deferred release window so delayed crashes cannot pass the smoke test.

### WinUI content pages
- Remove an unsupported Button.IsDefault initializer caught by the 1.1.33 build.
- Add timestamps to history entries and entrance transitions that use the app's Reduce Motion-aware transition factory.

### Validation
- Run 199 proved that the imported EGL surface could be created but still rendered transparent black. The legacy-handle correction is a targeted hypothesis based on the pinned ANGLE OpenSharedResource implementation; it must pass the GPU pixel probe before the experimental renderer is considered usable.

### Browser content pages
- Move History and Settings into the browser's WinUI content viewport, alongside the native start page, instead of showing History as a flyout and Settings as a modal page.
- Add searchable browsing history, an explicit clear-history confirmation, and in-page Settings navigation with Save, Cancel and Clear browsing data actions.
- Keep the native WebKit HWND hidden while an internal page occupies the content viewport.

### ANGLE shared-texture diagnostics
- Remove the unrelated EGL_BIND_TO_TEXTURE_RGBA config-selection constraint; it applies to eglBindTexImage on ordinary pbuffers and can leave ANGLE with zero otherwise-compatible configs.
- Keep EGL_TEXTURE_TARGET=EGL_TEXTURE_2D and EGL_TEXTURE_FORMAT=EGL_TEXTURE_RGBA on the imported client-buffer surface, while creating the baseline pbuffer with only width and height attributes.

### Native tab teardown diagnostics
- Delay final WKView release after stopping loading and detaching callbacks, and trace the native release stages to help identify the exact failing teardown operation.
- Make the Release smoke test explicitly target the main WinUI HWND when creating and closing tabs, avoiding ambiguous foreground-window selection.
- Add UI automation coverage for the in-content History page.

### Validation
- The preceding 1.1.32 workflow built and installed the EXE installer, but its Release launch smoke test and ANGLE enforcement failed. This version must pass the full workflow before it is treated as a validated release.

### Native tab stability
- Split WebKit tab teardown into a prepare phase and a deferred final release on the next WinUI dispatcher turn, allowing pending native window work to drain after callbacks are detached and loading is stopped.
- Forward WebKit-unhandled keyboard events to the owning WinUI window so browser shortcuts work while native page content has focus.
- Extend the Windows smoke test to close tabs during navigation as well as after loading.

### ANGLE shared-texture diagnostics
- Require a texture-bindable RGBA EGL config and explicitly set EGL_TEXTURE_2D / EGL_TEXTURE_RGBA on the imported shared-handle pbuffer.
- Flush GLES commands before verifying the D3D11 producer texture.

## [1.1.31] - 2026-11-13

### Build pipeline resilience
- Run the stable WinUI compile, installer packaging and UI smoke tests even when the experimental ANGLE rendering probe fails.
- Upload the standalone installer and smoke-test artifacts before enforcing the ANGLE probe result, so an experimental GPU issue no longer hides a usable stable build.

### ANGLE render diagnostics
- Compare a normal EGL pbuffer against the imported NT shared-handle pbuffer using framebuffer completeness, GL errors and GLES pixel readback.
- Log the selected EGL config's color sizes and renderable/surface bits to isolate why imported-surface drawing currently reads back transparent black.

## [1.1.30] - 2026-11-13

### Native tab lifetime regression coverage
- Make managed tab-close guards exception-safe and keep the browser's original native page alive through repeated tab churn.
- Extend the Windows Release smoke test to create, navigate and close three native WebKit tabs, checking that Blaze remains alive and the original page remains selected.
- Verify that both startup behavior choices are exposed in the Settings UI automation tree.

### ANGLE EGL diagnostics
- Read a diagnostic GLES pixel from the imported pbuffer, call eglSwapBuffers, and log the producer device's mapped BGRA pixel.
- Separate a GLES render mismatch from an EGL presentation/flush or D3D shared-resource visibility mismatch.

## [1.1.29] - 2026-11-13

### Tab stability
- Make tab close idempotent and clear the managed native handle before destroying its WebKit view.
- Track native-handle-to-tab identity so delayed WebKit close callbacks cannot accidentally target a different tab after a handle is reused.
- Detach WebKit page clients before releasing the view and serialize shared WebKit engine reference-count changes.

### Startup behavior
- Add a General setting to open a new tab at launch or restore the previous tab session.
- Preserve the existing continue-session behavior as the default for upgraded installations.

### ANGLE shared-texture probe
- Select EGL pbuffer configs by testing the actual NT shared-handle import rather than comparing EGL_NATIVE_VISUAL_ID to a DXGI_FORMAT enum.
- Report each candidate config and EGL error when the pinned ANGLE runtime rejects an import.

## [1.1.28] - 2026-11-13

### WebKit GPU composition experiment
- Add a real ANGLE EGL import/render test using the staged WinCairo runtime's D3D11 device.
- Create a BGRA8 texture on ANGLE's own D3D11 device, import it as an EGL pbuffer, render a GLES test pattern, and verify the D3D11 pixels.
- Keep the staging readback diagnostic-only; no CPU readback is introduced into the browser renderer.
- Preserve the stable WinCairo HWND path while the WebKit WCScene output adapter and WinUI composition presentation are still under development.

### Address bar keyboard behavior
- Defer focus restoration an additional dispatcher turn so AutoSuggestBox can finish its internal Enter and suggestion-popup handling.
- Extend the Release UI smoke test to assert that Enter moves keyboard focus away from the address bar.



## [1.1.27] - 2026-11-12

### WebKit GPU composition experiment
- Add a standalone x64 D3D11 capability probe for NT shared textures, cross-process handle duplication, keyed-mutex synchronization and child-process content verification.
- Report ANGLE EGL vendor, version, display/device extensions and D3D adapter identity when the staged runtime exposes an initializable ANGLE display.
- Build and run the probe in the experimental Windows Actions workflow without changing the stable WinCairo HWND renderer or enabling an unverified composition mode.
- Explicitly distinguish successful D3D shared-resource transport from the still-unproven ANGLE texture import and WebKit WCScene output integration.

### Browser consistency
- Add a slim, native WinUI page-loading progress indicator driven by WebKit's estimated load progress; hide it for the start page and idle tabs.
- Align the favicon discovery HTTP User-Agent with the browser's Safari-style Windows User-Agent.



## [1.1.26] - 2026-11-12

### Browser identity
- Change the Safari-style user agent platform token from macOS to Windows while retaining the configured Safari/WebKit version tokens.

### Release validation
- Fix the Release UI smoke test's native-content screenshot selector: target the owned WinCairo host window explicitly instead of selecting both top-level Blaze windows by process.
- Capture the Settings-over-WebKit state against the WinUI main window explicitly.
- Keep the installed-app integration test and the native WinCairo content checks mandatory.

## [1.1.25] - 2026-11-12

### Dialogs and settings
- Hide the native WebKit owned-window host while WinUI modal dialogs are open, then restore the selected page after the dialog closes. This prevents WebKit pixels from covering settings, permission prompts and other modal UI.
- Split browser settings into General, Privacy, Appearance and Advanced tabs with independently scrollable content.
- Keep the experimental ContentExternalOutputLink investigation explicit: the API is an external composition output target, not a supported HWND-import bridge for WinCairo. No nonfunctional rendering-mode switch is exposed.
- Make the Windows UI smoke test address the app by process ID rather than retaining a potentially stale top-level HWND across UI automation operations.

### Diagnostics
- Continue recording toolbar actions in the crash log; the intermittent native crash remains under investigation and is not marked as resolved.


## [1.1.24] - 2026-11-12

### Browser stability and input
- Restore adaptive TabView tab widths: favicons remain left of titles, while each tab sizes naturally from its title instead of forcing every tab to 112 DIPs.
- Move keyboard focus out of the address bar after Enter submits a navigation, and close the suggestion popup.
- Add privacy-safe toolbar action breadcrumbs to the existing crash log so a native failure can be correlated with the last browser action.
- Set the WinCairo page user agent to Safari 27.2 for macOS, using Apple's frozen macOS platform token and Safari's current version token.

### Hosting options
- Keep the proven Win32 owned-window host as the active rendering path. ContentExternalOutputLink remains experimental in the current stable Windows App SDK dependency and is not silently presented as an implemented rendering mode; its integration requires the experimental API projection and a verified WinCairo external-output connection.

## [1.1.23] - 2026-11-12

### Native WebKit presentation in WinUI 3
- Host each native WinCairo view in a borderless owned popup HWND above the WinUI window, avoiding the final WinUI DirectComposition layer that can obscure ordinary child HWND output.
- Keep the WebKit HWND as a normal native child of the popup host so WebKit's standard compositor and native mouse, keyboard, and wheel input paths remain intact.
- Position the owned host relative to the existing desktop z-order, keeping it above Blaze's WinUI owner without raising it over unrelated foreground or topmost windows.
- Maintain one owned popup host per browser tab, position it from the XAML content bounds, track owner-window moves and DPI changes, hide inactive tabs, and destroy each host with its WebKit view.
- Hide the native host while the start page or engine-error overlay is visible, leaving WinUI chrome and dialogs unobstructed.

### Browser chrome and address editing
- Put tab favicons before titles and reduce the custom tab header width to 112 DIPs, truncating long titles within the remaining space.
- Use the built-in globe mark when a site has no usable favicon or an icon fails to decode.
- Keep the address bar's typed text stable while it is focused or being edited; synchronize it from the active page only after focus leaves or navigation/tab selection changes.
- Extend the Release smoke test to open a local HTML page through native WinCairo, confirm its owned host is visible and above the WinUI surface, and verify its document title reaches the tab UI.

## [1.1.22] - 2026-11-12

### Website favicons
- Display each tab's site favicon at the trailing edge of its tab header, with a Segoe Fluent globe fallback when the site does not provide a supported icon.
- Load site-declared icon links for the native start-page quick links instead of showing generic glyphs when a favicon is available.
- Cache favicon files under the app's temporary LocalAppData cache, deduplicate concurrent requests, expire entries after 30 days and cap the cache at 256 files.
- Discover icons from the site's HTML link metadata and fall back to the origin's /favicon.ico; ignore non-HTTP(S), oversized and unsupported image payloads.

### WinCairo content rendering
- Match the upstream WinCairo MiniBrowser's rounded native-window DPI backing-scale configuration.
- Keep the WebKit child HWND above sibling native windows and translate page bounds relative to the top-level Window root, including the custom title-bar and Frame offsets.

### Release smoke test
- Replace the brittle multi-screen UI Automation suite with a focused Release launch check: confirm the app process, wait for its main WinUI window, capture a startup screenshot and preserve diagnostics.
- Keep the separate installed-EXE integration test, including actual installer launch and uninstall checks.



## [1.1.21] - 2026-11-13

### Unpackaged WinUI installer resource and launch validation
- Copy the generated WinUI PRI resource index from the build output into the self-contained publish directory; fail publishing if the index is missing.
- Validate the PRI resource index alongside the native WebKit host and runtime before building the installer.
- Add an Inno Setup integration test that silently installs to the configured per-user path, launches the installed executable, verifies the WinUI address control, captures startup diagnostics and a screenshot, and verifies silent uninstall.
- Keep the separate dotnet-run smoke test for the build-output launch path; the installed-app test independently exercises the published installer payload.



## [1.1.20] - 2026-11-12

### Release smoke-test argument and process discovery fix
- Pass MSBuild properties to `dotnet run` using the explicit `--property:` syntax so they are not confused with the CLI's `-p/--project` option.
- Identify the launched `Blaze.exe` by its parent process ID instead of converting the `Win32_Process.CreationDate` DMTF value, which failed on the GitHub Actions runner.
- Keep the Release smoke test on the .NET CLI launch path and preserve stdout/stderr diagnostics.


## [1.1.19] - 2026-11-12

### Release startup and smoke-test path
- Launch the Release configuration through `dotnet run` rather than directly starting the published apphost, matching the .NET CLI development launch path while still testing Release binaries.
- Discover and scope UI automation to the actual `Blaze.exe` child process created by `dotnet run`, and retain stdout/stderr as CI diagnostics if startup fails.
- Use the same ReadyToRun-disabled Release setting for restore, build, run and publish to eliminate conflicting Release configurations during diagnosis.
- Preserve the standalone self-contained Inno Setup installer and the pinned Apple WebKit WinCairo runtime.


## [1.1.18] - 2026-11-12

### Release startup diagnostics
- Disable ReadyToRun for the unpackaged WinUI Release publish while isolating a Release-only XAML activation failure; keep the app self-contained.
- Fix the smoke test's Release executable path construction using nested Join-Path calls.
- Enable per-process Windows Error Reporting full dumps during CI smoke testing and upload any dump with the diagnostic artifacts to expose hidden WinUI stowed exceptions.
- Keep testing the actual Release publish output and preserve the standalone Inno Setup installer.


## [1.1.17] - 2026-11-12

### Release-build smoke testing and startup diagnostics
- Run UI smoke tests against the published Release executable that is packaged into the standalone installer, rather than rebuilding and testing a separate Debug executable.
- Expand startup logs with product and assembly versions, Windows App SDK assembly version, process and OS architectures, process bitness and application base directory.
- Record XAML resource names, numeric XAML line metadata when available, exception types, HRESULTs, target sites, full stack traces and nested/aggregate/loader exceptions.
- Flush diagnostic log entries to disk so startup failures are less likely to lose their final diagnostic record.
- Keep page content, browsing URLs, cookies, form values and arbitrary exception data out of routine logs.


## [1.1.16] - 2026-11-12

### UI automation reliability and toolbar polish
- Keep all smoke-test interactions bound to the exact Blaze main-window HWND after initial process-scoped discovery, avoiding foreground-window selection when WebKit creates helper or popup windows.
- Correct the loading-state glyph escape so the combined Reload/Stop button displays the actual Segoe Fluent Stop and Reload icons instead of placeholder squares.
- Align assembly, file, informational and package version metadata with product version 1.1.16 so diagnostic logs and Windows file properties report the shipped build accurately.


## [1.1.15] - 2026-11-12

### Browser chrome and navigation state
- Replace text-labelled toolbar commands with compact icon-only Segoe Fluent buttons while retaining accessible names and tooltips.
- Combine Reload and Stop into one button that switches its glyph, accessible name and action using WebKit estimated page-load progress.
- Expose page-load progress through the native WinCairo host instead of guessing from a timer or navigation URL.

### Startup diagnostics
- Remove the title-bar raster icon-source XAML that could fail while constructing the window; the app icon remains assigned to the native window in code.
- Initialize crash logging before application XAML resources and include HRESULTs for each exception in the inner-exception chain.


## [1.1.14] - 2026-11-12

### Startup crash diagnostics
- Add persistent diagnostic logging at `%LOCALAPPDATA%\\Blaze\\Logs\\blaze.log`.
- Record startup milestones and full exception details for WinUI unhandled exceptions, AppDomain unhandled exceptions and unobserved task exceptions.
- Keep logging best-effort so a logging failure cannot prevent the browser from launching; rotate the previous log when it exceeds 2 MiB.
- Deliberately exclude browsing URLs, page contents, cookies and form data from routine logs.
- Attach the diagnostic log to UI smoke-test artifacts when it exists, including failed test runs.

### UI smoke-test reliability
- Use real keyboard input for AutoSuggestBox address/search fields because UI Automation does not consistently expose a writable ValuePattern for those controls.
- Verify the start-page search and individual shortcut buttons without requiring the GridView container's AutomationId to appear in the interactive tree.


## [1.1.12] - 2026-11-12

### Standalone installer reliability
- Detect and use the preinstalled Inno Setup compiler from PATH, standard installation locations, Chocolatey package locations, or its registered install location before attempting any installation.
- Treat Chocolatey installation as a fallback only, avoiding the non-zero “already installed” result from breaking packaging.
- Create the release output directory before invoking Inno Setup and verify the exact versioned standalone installer exists.

### Installation documentation and keyboard coverage
- Replace obsolete MSIX and test-certificate instructions with the actual per-user standalone EXE install and uninstall workflow.
- Extend live WinUI automation to exercise Ctrl+L, Ctrl+T, Ctrl+W and Ctrl+R against the native browser window and WebKit page.

## [1.1.11] - 2026-11-12

### Standalone Windows installer
- Replace the MSIX-wrapped EXE installer with a genuine per-user Inno Setup installer containing the unpackaged, self-contained WinUI application and the pinned Apple WebKit WinCairo runtime.
- Publish the WinUI app with `WindowsPackageType=None`; remove MSIX generation, development-certificate creation, certificate trust instructions and App Installer dependencies from release automation.
- Install under `%LOCALAPPDATA%\\Programs\\Blaze`, add Start menu and optional desktop shortcuts, and provide normal uninstall support without touching browser profile data in `%LOCALAPPDATA%\\Blaze`.

### Browser keyboard shortcuts
- Add Ctrl+L to focus the address/search box, Ctrl+T to open a tab, Ctrl+W to close the active tab, and Ctrl+R to reload the current page.
- Implement shortcuts as native WinUI `KeyboardAccelerator` controls so they participate in WinUI keyboard routing and accessibility conventions.

## [1.1.10] - 2026-11-12

### Release build restore consistency
- Restore the WinUI project using the same Release configuration and ReadyToRun setting as the subsequent build, ensuring the `win-x64` ReadyToRun runtime package is present when `dotnet build --no-restore` runs.
- Keep ReadyToRun optimization enabled for the packaged x64 release rather than disabling it to work around an incomplete restore graph.

## [1.1.9] - 2026-11-12

### WinUI XAML compiler reliability
- Fix the missing `Microsoft.UI.Xaml.Automation` namespace import used by native WinUI accessibility metadata.
- Build the eight native start-page shortcut buttons directly with WinUI controls instead of a runtime-bound `DataTemplate` that triggered the WinUI 2.6.5 MarkupCompilePass2 internal error.
- Preserve shortcut names, AutomationIds, Fluent glyphs, sizing, keyboard-accessible Button semantics, and the existing GridView layout.

## [1.1.8] - 2026-11-12

### WinUI build compatibility
- Match the WinUI 2.6.5 `TabView.AddTabButtonClick` event's actual `object` event-argument signature.
- Use runtime bindings for the native start-page shortcut template so the XAML compiler does not require an unresolved project-local `x:DataType` during MarkupCompilePass2.
- Keep the shortcut list strongly typed in the page while binding its public item properties through the standard WinUI `Binding` mechanism.

## [1.1.7] - 2026-11-12

### WinCairo native host compatibility
- Preserve WebKit's const-qualified JavaScript dialog result-listener pointers across the native callback ABI.
- Keep the retained listener pointer const-qualified through asynchronous WinUI dialog resolution, matching upstream WinCairo public headers.
- Fix the native host compile errors affecting JavaScript alert, confirm, prompt and before-unload dialogs.

## [1.1.6] - 2026-11-12

### SDK workflow tracking
- Use Git's repository-relative forward-slash pathspecs when resolving the latest SDK workflow/specification revision.
- Inspect a wider recent-run window so app-only builds reliably find the matching pinned SDK build.

## [1.1.5] - 2026-11-12

### SDK bootstrap reliability
- Resolve the latest commit that changed the pinned SDK workflow or specification and monitor that exact Actions run.
- Let app-only commits reuse an SDK build already running or completed for the unchanged pinned engine revision.
- Detect failed, timed-out or startup-failed SDK builds promptly without mistaking a superseded/cancelled run for the active SDK build.

## [1.1.4] - 2026-11-12

### WebKit SDK staging and provenance
- Stage the public C API headers from WebKit's actual CMake output directory, `WebKit/Headers`, instead of the nonexistent `Release/include` path.
- Normalize the WebKit, JavaScriptCore, WTF, PAL, WebGPU, ANGLE and bmalloc public header trees into the SDK include root.
- Capture the package database from the build's manifest-installed vcpkg tree, rather than querying the unrelated Visual Studio vcpkg installation.
- Record header and vcpkg package counts and their provenance in the SDK manifest.
- Keep the successful pinned Release build cache intact so SDK staging retries do not repeat the multi-hour WebKit compile.
- Make the browser workflow detect a failed SDK workflow for the pinned SDK-definition revision and fail promptly instead of waiting for the full release timeout.

## [1.1.3] - 2026-11-11

### Native WinUI start page
- Make new tabs open a native WinUI start page instead of loading a third-party search website by default.
- Add a native start-page search box and accessible, keyboard-focusable quick-link buttons in a responsive GridView.
- Keep custom home-page URLs optional; leaving the setting blank restores the native Blaze start page.
- Migrate the previous uncustomized Google homepage default to the native start page.

### WebKit SDK build and staging
- Fix the SDK staging path: the pinned WinCairo build emits WebKit2.dll and helper processes under WebKitBuild/Release/bin; normalize those outputs to the SDK's stable bin64 layout.
- Validate WebKit2.dll, WebKit2.lib, WebKitWebProcess, WebKitNetworkProcess and WKView.h before publishing.
- Save the successful Release build cache before staging so packaging/provenance retries do not rebuild WebKit.
- Keep Release configuration and omit optional API/layout test, WebDriver and MiniBrowser targets from the production SDK build.

## [1.1.2] - 2026-11-11

### Browser settings
- Add editable home-page and default search-engine preferences.
- Route address-bar searches through the selected Google, Bing, DuckDuckGo or Brave Search provider.
- Persist the selected preferences alongside the existing browser settings.

## [1.1.1] - 2026-11-11

### Build reliability
- Cancel superseded SDK and browser workflow runs so a new commit for the same ref can start immediately.
- Build the pinned WebKit checkout from a short physical path on the runner's workspace volume; avoid SUBST aliases that caused HarfBuzz's Meson-generated Python code to mix drive letters.

## [1.1.0] - 2026-11-11

### WebKit SDK consumption
- Remove the obsolete short-lived Actions artifact download from normal browser builds; the validated versioned SDK release is now the single source for the WebKit runtime and native host inputs.
- Keep normal builds independent of the engine-build workflow after the SDK release is published.


## [1.0.9] - 2026-11-11

### CI reliability
- Explicitly reset PowerShell's inherited native exit code after the expected missing-SDK release check so the SDK bootstrap job can continue to the pinned WebKit build.


## [1.0.8] - 2026-11-11

### CI reliability
- Handle the expected non-zero GitHub CLI result when the versioned WebKit SDK release does not exist yet, without terminating the PowerShell preflight or browser wait loop.
- Keep the SDK build and normal browser build independently runnable while the first engine artifact is being created.


## [1.0.7] - 2026-11-11

### WebKit SDK distribution
- Separate the pinned WinCairo engine build from normal browser builds.
- Publish a versioned x64 SDK release containing WebKit runtime binaries, resources, public headers, import libraries, dependency DLLs and provenance.
- Validate the SDK archive SHA-256 and pinned WebKit/vcpkg baselines before compiling the native host and WinUI app.
- Use a short same-volume drive alias for WebKit source, vcpkg and build paths to avoid ICU's Windows object-path limit.

### Popup prompt
- Use one native dismissal action labelled Ignore; closing the prompt has the same deny-this-request behavior.
- Add UI smoke coverage for the single Ignore action and wait for settings persistence before asserting it.


## [1.0.6] - 2026-11-11

### Native popup routing
- Enable WebKit's `JavaScriptCanOpenWindowsAutomatically` preference so script-created window requests reach the native WinUI policy instead of being silently blocked by the engine default.
- Keep the final decision in the browser: Automatic prompts, Always allow opens a tab, and Always deny rejects the request.

## [1.0.5] - 2026-11-11

### Popup settings accessibility and coverage
- Give each native WinUI popup-policy RadioButton its own AutomationId and accessible name.
- Exercise Automatic, Always allow and Always deny end-to-end in the UI smoke test, including persistence and the actual `window.open()` result.
- Verify the WinUI RadioButtons item and RadioButton content APIs against the restored app references in CI.

## [1.0.4] - 2026-11-11

### Popup settings and native callback reliability
- Name the request-permission policy Automatic and keep it as the default setting.
- Pass native popup handlers to the WinUI DispatcherQueue using its native delegate signature.

## [1.0.3] - 2026-11-11

### Browser settings and popup control
- Add a Pop-up windows settings section with Automatic, Always allow and Always deny policies.
- Automatic mode displays a native WinUI Allow / Ignore / Cancel dialog for each requested pop-up; ignored or cancelled pop-ups are destroyed without opening a tab.
- Add UI automation coverage for the default Ask flow and the settings control.

### WebKit build reliability
- Keep the WinCairo source, vcpkg and build directories on the same drive to prevent Python path-relativization failures in HarfBuzz.
- Enable Windows long-path support and use a shorter checkout directory.
- Reuse the pinned WebKit build cache after the first successful engine build, avoiding a full WebKit rebuild on subsequent browser commits.

## [1.0.2] - 2026-11-11

### Build reliability
- Repair the Windows Actions workflow's PowerShell product-version validation and remove duplicated packaging/release fragments so the YAML parses as one pipeline.
- Run CI when workflow files change, so future workflow edits are validated.

### Browser development
- Handle JavaScript `window.open()` and `target="_blank"` through WebKit's native `createNewPage` callback, creating a new tab that shares the opener's WebKit page configuration and website data store.
- Route JavaScript `window.close()` back to the owning WinUI tab, while preserving at least one browser tab.
- Extend the live WebKit UI smoke test to exercise popup creation and closure.

## [1.0.1] - 2026-11-11

### Build and packaging reliability
- Replace the temporary port-labelled version with Blaze's normal product version.
- Fix the WinCairo build environment so Ninja remains discoverable after Visual Studio initializes its compiler environment.
- Print the vcpkg manifest-install log and CMake diagnostics directly in the Actions job when the engine build fails.
- Keep diagnostic artifact upload failures from obscuring the original engine-build failure.

### WinUI experience
- Use native Segoe Fluent iconography alongside labels for the primary browser toolbar commands.

## [1.0.0] - 2026-11-11

### Initial browser implementation
- Establish Blaze as a native WinUI 3 browser using the upstream WebKit WinCairo engine, with no Chromium, Gecko, or WebView2 fallback.
- Add tabbed browsing, address/search navigation, back/forward/reload/stop/home commands, bookmarks, browsing history and downloads.
- Persist browser sessions, settings, cookies, cache, IndexedDB, local storage and service-worker data.
- Add per-site geolocation and notification permission decisions, JavaScript/image preferences, and site-data clearing.
- Add native WinUI JavaScript alert, confirm, prompt and before-unload dialogs.
- Add motion-aware WinUI transitions and a persistent Reduce motion preference.
- Package the app as a signed MSIX with a per-user EXE installer and GitHub Actions build workflow.

### Limitations at the 1.0.0 baseline
- Popup/new-window handling, certificate exception UI, camera/microphone device selection and broader security hardening were not yet implemented.
- The upstream WinCairo build and complete clean-machine installation still require successful CI validation.
