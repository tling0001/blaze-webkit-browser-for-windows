# Installing Blaze

Each GitHub Release includes a standalone `Blaze-Setup-<version>-x64.exe` installer.

## Install

1. Download `Blaze-Setup-<version>-x64.exe` from the [GitHub Releases](https://github.com/tling0001/blaze-webkit-browser-for-windows/releases) page.
2. Run the installer as your normal Windows user. It installs to `%LOCALAPPDATA%\\Programs\\Blaze` and does not request administrator elevation.
3. Keep the optional desktop shortcut selected if you want one, then complete setup.
4. Launch **Blaze** from the Start menu or desktop shortcut.

The installer contains the unpackaged, self-contained WinUI 3 application, the native WebKit host, and the pinned upstream Apple WebKit WinCairo runtime. It does not install an MSIX package, import a test certificate, or require App Installer.

## Uninstall

Use **Settings → Apps → Installed apps → Blaze → Uninstall**, or launch **Uninstall Blaze** from the Start menu. Uninstalling removes the application files and shortcuts. Your browser profile and settings under `%LOCALAPPDATA%\\Blaze` are kept so a reinstall can preserve your data.

Blaze is currently distributed as a Windows x64 testing build. Review the [project README](README.md) for engine and browser-security limitations.
