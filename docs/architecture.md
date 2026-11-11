# Architecture and engineering decisions

## 1. Native UI

Use WinUI 3 (not WPF, WinForms, MAUI, Electron, or a web-based shell). Use the official WinApp CLI `winui-mvvm` scaffold, MVVM, platform controls, semantic `ThemeResource` brushes, and accessible keyboard/focus behavior.

The initial app shape is a browser workspace: a compact command/navigation area, a tab strip, an address/search field, and a content surface. Browser content must be hosted by WebKit, not a WinUI WebView control.

## 2. Browser engine

### Selected port

Use the upstream WebKit **Windows port** (formerly WinCairo). WebKit's own port documentation describes a 64-bit Windows port with Cairo graphics and libcurl networking. This is WebKit itself, not Chromium or Gecko.

### Integration boundary

Keep engine code behind a C ABI/native host boundary:

- C++ owns WebKit page/view objects, event-loop integration, process lifetime, navigation, history, downloads, permissions, and script messaging.
- C# owns WinUI presentation, browser chrome, tab/session view models, settings UI, and user workflows.
- Use HWND interop to parent the native WebKit view into a WinUI content host. Do not instantiate WebView2 or silently fall back to another engine.
- Define explicit ownership and disposal rules for every native handle and callback.

### Production-readiness gate

The upstream Windows port is described as a development/testing port and does not provide the same regular stable-release/security-advisory cadence as Apple's and Igalia's regularly released ports. Before shipping:

1. Pin a specific upstream WebKit revision and all dependency revisions.
2. Build the Windows port from source in a reproducible Windows CI job.
3. Package every required runtime DLL/data file with the application and validate clean-machine startup.
4. Establish a routine for upstream security updates and rebuilds.
5. Run WebKit layout tests plus browser-level navigation, media, downloads, permissions, and crash-recovery tests.
6. Publish the exact engine revision and build provenance with each release.

No unmaintained third-party binary bundle should be treated as a security-update strategy.

## 3. Initial scope

Build in this order:

1. WinUI 3 app shell and MVVM navigation.
2. Engine build pipeline and native hosting proof-of-concept.
3. Address/search, back/forward/reload/stop, tabs, and session restore.
4. History, bookmarks, downloads, find-in-page, and settings.
5. Privacy/security controls, site permissions, tracking protections where supported by the selected port, and update strategy.
6. Installer, signing configuration, release artifacts, and automated tests.

## 4. Explicit exclusions

- Chromium / Blink
- Gecko / Firefox
- Microsoft Edge WebView2
- Electron, CEF, or other Chromium wrappers
- Any engine fallback presented as WebKit
