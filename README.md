# WebKit Browser for Windows

A native Windows desktop browser with a WinUI 3 interface and the WebKit browser engine.

## Non-negotiable engine requirement

This project must embed **WebKit**. Chromium, Gecko/Firefox, Microsoft Edge WebView2, and browser-engine fallbacks are not acceptable.

The intended Windows engine is WebKit's upstream **Windows port** (historically called WinCairo). It is a genuine WebKit port, using Cairo for graphics and libcurl for networking. The application must integrate the engine through a native Win32/WinUI interop layer; WinUI's WebView2 control must not be used.

> The upstream Windows port is documented primarily as a development/testing port, not as a regularly released redistributable runtime. A production browser therefore needs a reproducible, pinned engine build, security-update policy, dependency packaging, and end-to-end validation before it can be called usable or secure. This is a first-class engineering task, not something to hide behind a placeholder WebView.

## Technology

- WinUI 3 / Windows App SDK, C#, MVVM
- Native WebKit Windows port, hosted through a small C++/Win32 interop component
- x64 Windows initially
- GitHub Actions for repeatable Windows builds and downloadable artifacts

## Getting started

The initial repository bootstrap uses Microsoft's WinApp CLI and the official `winui-mvvm` template. See [Bootstrap WinUI project](.github/workflows/bootstrap-winui.yml).

## Current status

- [x] Repository and engine constraints documented
- [x] WinUI 3 project bootstrap workflow
- [ ] Generated WinUI project committed
- [ ] WebKit Windows port pinned and built reproducibly
- [ ] Native WebKit hosting/interop
- [ ] Browser chrome, tabs, navigation, history, bookmarks, downloads, settings
- [ ] Security hardening and engine update process
- [ ] Windows installer and release artifacts

## Architecture

See [docs/architecture.md](docs/architecture.md) for the engine/UI boundary and implementation sequence.
