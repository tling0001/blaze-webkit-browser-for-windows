# Blaze

Blaze is a native Windows desktop browser built with WinUI 3 and the upstream WebKit Windows port (WinCairo).

## Engine

Blaze embeds **WebKit**, not Chromium, Gecko/Firefox, Edge WebView2, Electron, or CEF. The WinUI 3 application hosts WebKit's public Windows WKView C API through a small native C++/Win32 component.

**Live browser pages use WinCairo's GPU-accelerated `WCScene` / TextureMapper renderer.** The native host explicitly calls `WKViewSetUsesOffscreenRendering(false)`, selecting the normal GPU presentation path and disabling the alternative `ShareableBitmap` + `glReadPixels` CPU-readback path. This production renderer presents to the owned native HWND layered over the WinUI content. The separate ANGLE shared-texture/DirectComposition work is diagnostic groundwork; live `WCScene` frames are not yet transported into a WinUI composition swap chain.

The upstream WinCairo port is an x64 development/testing port rather than a regularly released consumer runtime. Blaze pins an exact WebKit revision and builds it from source in GitHub Actions. Treat releases as testing builds until the engine build, clean-machine installation, security-update process, and end-to-end browser tests are verified.

## Technology

- Native WinUI 3 / Windows App SDK, C#, MVVM, CommunityToolkit.Mvvm
- Native C++ bridge to WebKit's WinCairo WKView
- Windows x64 only
- GitHub Actions builds the pinned WebKit engine, self-contained unpackaged WinUI application, and standalone per-user Inno Setup `.exe` installer

## Browser functionality

- Official WinApp CLI WinUI MVVM scaffold
- Native child-window hosting for WebKit WKView; no WebView2 or Chromium fallback
- Native WinUI start page with a responsive quick-link grid, central search box and accessible native buttons
- Address/search, back/forward/reload/stop/home
- Optional custom home page (the native Blaze start page is the default) and default Google, Bing, DuckDuckGo or Brave Search provider
- Multiple tabs with per-tab native WebKit views and shared website data store/context
- Configurable pop-up handling: automatic permission prompts, always allow, or always deny
- JavaScript `window.open()` and `target="_blank"` open a new native browser tab; `window.close()` closes the owning tab
- Persistent sessions, browsing history and bookmarks
- Downloads into the Windows Downloads folder with progress and completion tracking
- Per-site geolocation and notification permission decisions; camera/microphone are denied until device-selection support exists
- Privacy settings for JavaScript, automatic image loading and clearing cookies/cache/site data
- Native WinUI password manager with per-user Windows Credential Locker storage, explicit password reveal, and confirmed deletion; secrets are not stored in browser JSON files
- Tab strip placement preference (above or below the navigation toolbar)
- Persistent Reduce motion preference; WinUI transitions are suppressed when enabled
- Native WinUI `ContentDialog`, `MenuFlyout`, `TabView`, `AutoSuggestBox`, `ToggleSwitch` and `Button` controls
- Native WinUI dialogs for WebKit JavaScript alert, confirm, prompt and before-unload requests
- Popup lifecycle handled by WebKit's `WKPageUIClientV13` `createNewPage` and `close` callbacks

## Build and release

See [`.github/workflows/build-windows.yml`](.github/workflows/build-windows.yml).

1. The separate SDK workflow checks out the pinned WebKit revision, enables Windows long paths, and builds the x64 WinCairo runtime in Release mode.
2. Save the completed WebKit build cache before packaging, then normalize upstream `bin` outputs into the versioned SDK's stable `bin64` layout.
3. Stage the public headers from WebKit's CMake `WebKit/Headers` output, record the actual manifest-installed vcpkg package database, validate required runtime processes, `WebKit2.dll`, `WebKit2.lib`, dependency DLLs, pinned baselines and SHA-256 provenance; then publish the SDK as an immutable GitHub Release. App-only commits reuse the SDK run associated with the most recent SDK workflow/specification change.
4. Normal Blaze builds download and validate that versioned SDK, compile the native `WebKitHost.dll`, and restore/verify the WinUI project with WinApp CLI and `winapp find-api`.
5. Compile and package the self-contained WinUI application.
6. Build the per-user `Blaze-Setup.exe` installer with Inno Setup.
7. Run WinApp UI automation against the native WinUI start page and live WebKit navigation, popup-tab creation and JavaScript tab closure.
8. Publish the standalone EXE installer and install guide as a versioned GitHub Release.

The password manager is connected to WebKit main-frame form events. Trusted focus on an empty password field can fill a saved credential for the exact HTTP(S) origin (with native account selection when multiple entries match), and trusted form submission opens a native save confirmation. The bridge uses a per-view nonce, accepts only main-frame prompts, validates the browser-reported security origin against the current page, and keeps secrets in Windows Credential Locker rather than profile JSON or diagnostics. The current implementation does not yet cover password fields embedded in cross-origin iframes or non-standard shadow-DOM login widgets. Certificate exception UI, camera/microphone device selection and broader security hardening remain in progress. The complete WinCairo build and clean-machine installation still require successful CI validation.

The project `Version` property is the single release-version source. Blaze uses normal product versions: `1.0.0`, `1.0.1` through `1.0.9`, then `1.1.0` and onward. Use [`Scripts/bump-version.ps1`](Scripts/bump-version.ps1) to increment the patch number, roll `1.0.9` to `1.1.0`, update the four-part Windows package version, and add dated release notes.

## Install

Download `Blaze-Setup-<version>-x64.exe` from [GitHub Releases](https://github.com/tling0001/blaze-webkit-browser-for-windows/releases). Run it as your normal user. It installs the self-contained browser under `%LOCALAPPDATA%\\Programs\\Blaze` without MSIX, test certificates, or administrator elevation.

For installation and uninstall instructions, see [`INSTALL.md`](INSTALL.md).

## Development

- Official WinUI scaffold: repository root
- Native WebKit bridge: [`Native/WebKitHost`](Native/WebKitHost)
- Engine API notes: [`docs/wincairo-integration.md`](docs/wincairo-integration.md)
- Architecture and release-readiness gates: [`docs/architecture.md`](docs/architecture.md)

## Known testing limitations

- Upstream WinCairo release cadence and security servicing are not equivalent to WebKit's regularly released Apple/Igalia ports.
- Camera and microphone access is denied; device picker and per-device permission model are not yet implemented.
- Certificate exception UI and broader browser security hardening still need implementation and testing.
- A successful GitHub Actions build, UI smoke test and clean Windows installation are required before calling the package usable.
