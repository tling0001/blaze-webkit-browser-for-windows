param(
    [Parameter(Mandatory = $true)][string]$PackagePath,
    [Parameter(Mandatory = $true)][string]$CertificatePath
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) { throw "Blaze package was not found: $PackagePath" }
if (-not (Test-Path -LiteralPath $CertificatePath -PathType Leaf)) { throw "Blaze signing certificate was not found: $CertificatePath" }

# The release certificate is a public, per-user test certificate. Never install
# private signing material from an installer.
Import-Certificate -FilePath $CertificatePath -CertStoreLocation 'Cert:\CurrentUser\Root' | Out-Null
Import-Certificate -FilePath $CertificatePath -CertStoreLocation 'Cert:\CurrentUser\TrustedPeople' | Out-Null
Add-AppxPackage -Path $PackagePath -ForceUpdateFromAnyVersion
Write-Host 'Blaze was installed successfully.'
