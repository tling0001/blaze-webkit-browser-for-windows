# Blaze release notes

## [2026.11.0-port.4] - 2026-11-11

### Native dialogs and motion
- Bridge WebKit JavaScript alert, confirm, prompt and before-unload requests to native WinUI ContentDialogs.
- Retain WebKit result listeners across asynchronous dialog presentation and always resolve them on dismissal or failure.
- Add UI smoke coverage for sequential JavaScript dialogs and the reduced-motion settings dialog.
- Disable MenuFlyout open/close animations when Blaze Reduce motion or the Windows accessibility animation setting is active.

### Native host and reliability
- Define the missing native WebKit view/download context structures and forward declaration used by the C++ bridge.
- Move the persistent WebKit profile under `%LOCALAPPDATA%\Blaze\WebKitData`.
- Serialize and atomically replace browser-state JSON saves to avoid concurrent writes and partial files.
- Keep native callback state rooted for the process lifetime so late download/dialog callbacks cannot dereference a freed GCHandle.
- Use a temporary short drive mapping for the WebKit build to keep ICU/vcpkg CMake object paths within Windows limits.
- Preserve vcpkg/CMake diagnostics when the WebKit engine build fails.

## [2026.11.0-port.3] - 2026-11-11

### User experience
- Rename the app, executable, package identity and installer to Blaze.
- Add a persistent Reduce motion setting and motion-aware WinUI transitions.
- Serialize native ContentDialog presentation and animate dialog content without replacing the system dialog surface.
- Keep toolbar actions as native XAML Buttons with consistent default WinUI sizing.

### Windows installer and CI
- Add a per-user Blaze-Setup.exe installer that installs the signed WinUI package.
- Fix WebKit CI dependency discovery to use the actual Chocolatey XAMPP install path (C:\xampp).
- Verify WinUI transition APIs against the restored app references before compiling.
- Repair the release-version script and keep project, package and release-note versions synchronized.

### Known limitations
- WinCairo remains an upstream testing port; successful end-to-end build and installation validation are still required.

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
