# Installing a testing MSIX build

Each GitHub Release includes the signed MSIX, the matching public CER, and this install guide.

The release uses a test signing certificate, not a production publisher certificate.

1. Download the MSIX and matching CER from the same release.
2. Open PowerShell as your normal user and trust the test certificate for this user:

   Import-Certificate -FilePath .\WebKitBrowser-signing.cer -CertStoreLocation Cert:\CurrentUser\TrustedPeople

3. Install the package:

   Add-AppxPackage -Path .\WebKitBrowser-<version>-x64.msix

4. Launch WebKit Browser from Start.

The private PFX key is not included in releases. Do not import this test certificate into the machine-wide Trusted Root store. To remove the app, uninstall WebKit Browser from Windows Settings > Apps > Installed apps. Remove the test certificate from Current User > Trusted People if it is no longer needed.
