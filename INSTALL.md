# Installing Blaze

Each GitHub Release includes a recommended `Blaze-Setup-<version>-x64.exe` installer, a signed MSIX payload, and the matching public test certificate.

## Recommended: EXE installer

1. Download `Blaze-Setup-<version>-x64.exe` from the GitHub Release.
2. Run it as your normal Windows user. It does not request administrator elevation.
3. The setup wizard imports the release's public test certificate into the current-user certificate stores and installs the signed WinUI package.
4. Launch **Blaze** from Start.

## Manual MSIX installation

If you prefer to install the package manually, download the MSIX and matching CER from the same release:

```powershell
Import-Certificate -FilePath .\Blaze-signing.cer -CertStoreLocation Cert:\CurrentUser\Root
Import-Certificate -FilePath .\Blaze-signing.cer -CertStoreLocation Cert:\CurrentUser\TrustedPeople
Add-AppxPackage -Path .\Blaze-<version>-x64.msix
```

The private PFX signing key is never included in releases. This is a testing certificate for Blaze builds only. Remove Blaze from Windows Settings > Apps > Installed apps. If you no longer use testing builds, remove the matching certificate from Current User > Trusted Root Certification Authorities and Current User > Trusted People.
