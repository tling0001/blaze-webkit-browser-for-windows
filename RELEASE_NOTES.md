# Blaze release notes

## [1.0.3] - 2026-11-11

### Browser settings and popup control
- Add a Pop-up windows settings section with Ask (recommended), Always allow and Always deny policies.
- Ask mode displays a native WinUI Allow / Ignore / Cancel dialog for each requested pop-up; ignored or cancelled pop-ups are destroyed without opening a tab.
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
