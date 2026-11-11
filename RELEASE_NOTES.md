# WebKit Browser release notes

## [2026.11.0-port.1] - 2026-11-11

### Initial WinUI 3 implementation
- Generate the application from Microsoft's official WinApp CLI WinUI MVVM template.
- Establish a native WinCairo hosting boundary using WebKit's public Windows WKView API.
- Begin the browser shell, tab management, navigation, history, bookmarks, settings, and session persistence.
- Add an x64-only Windows build and versioned GitHub Release pipeline.
- Add a configurable tab strip placement preference (above or below the navigation toolbar).

### Engine status
- WebKit's Windows port is x64-only. The release remains a testing build until the pinned WebKit runtime is built, packaged, and validated end-to-end.
