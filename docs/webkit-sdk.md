# Versioned WebKit SDK

Blaze builds upstream WebKit WinCairo at the exact source revision and dependency baselines recorded in eng/WebKitSdk.json. The engine is built by the Build versioned WebKit SDK workflow; normal browser builds download the immutable release, verify its SHA-256 and provenance, compile Native/WebKitHost, and then build/package the WinUI app.

## Release contents

- A ZIP containing WebKit runtime binaries/resources, public headers, import libraries, WebKitRequirements DLLs, app-local Visual C++ runtime DLLs and dependencies.lock.txt.
- sdk-manifest.json with source commit, port, architecture, configuration, both vcpkg registry baselines, toolchain and archive SHA-256.
- sdk.sha256 as an independent checksum.

Treat SDK releases as immutable. When source revision, vcpkg baselines, triplet, compiler/build options or SDK layout changes, update eng/WebKitSdk.json and publish a new release tag. Never overwrite an existing release.

## Separate binary repository (recommended)

The workflows default to publishing the SDK release in the Blaze repository. For cleaner separation, create a public repository named tling0001/blaze-webkit-sdk and initialize it with a README so it has a main branch. Then configure these repository settings in blaze-webkit-browser-for-windows:

1. Repository variable WEBKIT_SDK_REPOSITORY = tling0001/blaze-webkit-sdk.
2. Repository secret WEBKIT_SDK_TOKEN = a fine-grained token with Contents: Read and write on blaze-webkit-sdk.

The public SDK repository lets pull-request builds download releases without exposing a write token. Keep the secret scoped only to the SDK repository. The SDK workflow uses it to publish releases; the browser workflow uses it to download and verify assets. The current-repository GITHUB_TOKEN is used to dispatch a browser build after a manual SDK rebuild.

## Bootstrap

A push changing the SDK workflow or eng/WebKitSdk.json starts the SDK build. A browser build triggered by the same change waits for that release. You can also run Actions → Build versioned WebKit SDK → Run workflow manually. A manual run triggers a browser build after publishing or confirming the SDK release.

## Why use our own artifact?

WebKit's Windows-port documentation describes downloading a green Buildbot compile-webkit archive and the matching WebKitRequirements release, then placing dependency DLLs beside MiniBrowser. That upstream prebuilt route is useful, but it does not automatically guarantee the exact Blaze WebKit commit, pinned registry baselines, compiler configuration or the files needed to link Blaze's native WKView host. Blaze therefore publishes its own validated SDK. See https://docs.webkit.org/Ports/WindowsPort.html#downloading-build-artifacts-from-buildbot.
