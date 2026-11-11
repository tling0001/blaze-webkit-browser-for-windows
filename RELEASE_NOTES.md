# WebKit Browser release notes

## [2026.11.0-port.2] - 2026-11-11

### Browser implementation
- Complete the WinUI 3 browser shell with address/search, back/forward/reload/stop/home, tabs, bookmarks and history.
- Host native WebKit WKView child windows through a C++/Win32 bridge and share the WebKit context and website data store across tabs.
- Persist cookies, caches, IndexedDB, local storage and service-worker data under LocalAppData.
- Add native WebKit downloads to the Windows Downloads folder, with progress and completion tracking.
- Add per-site geolocation and notification permission decisions; camera and microphone are denied until device selection is implemented.
- Add privacy settings for JavaScript, automatic image loading, and clearing cookies/cache/site data.
- Add the test preference for placing the tab strip above or below the navigation toolbar.
- Restore browser tabs, bookmarks, history, settings and downloads across app restarts.

### Windows packaging and release
- Restore packaged WinUI deployment and use WinApp CLI to produce a signed x64 MSIX.
- Add a public test certificate and installation guide to each GitHub Release.
- Build the pinned WinCairo engine and native bridge in GitHub Actions.

### Known limitations
- The package remains a testing build pending a successful full engine build, clean-machine install and end-to-end test.
- Popup windows, JavaScript dialog UX, certificate exception UI, and camera/microphone device selection remain outstanding.
- The upstream WinCairo port does not have the same regular security release cadence as Apple's and Igalia's WebKit ports.

## [2026.11.0-port.1] - 2026-11-11

### Initial project bootstrap
- Generate the app from Microsoft's official WinApp CLI WinUI MVVM template.
- Establish the WebKit WinCairo WKView hosting architecture and x64 GitHub Actions pipeline.
- Add project architecture notes and the initial version/release notes workflow.
