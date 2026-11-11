#define AppVersion GetEnv("BLAZE_VERSION")
#ifndef AppVersion
  #error BLAZE_VERSION must be set by the release workflow.
#endif

[Setup]
AppId={{44C86CE5-2F2D-412F-9565-A731B6070756}
AppName=Blaze
AppVersion={#AppVersion}
AppPublisher=Blaze contributors
CreateAppDir=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64
OutputDir=..\release-assets
OutputBaseFilename=Blaze-Setup-{#AppVersion}-x64
SetupIconFile=..\Assets\AppIcon.ico
Uninstallable=no
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Files]
Source: "..\release-assets\Blaze-{#AppVersion}-x64.msix"; DestDir: "{tmp}"; DestName: "Blaze.msix"; Flags: deleteafterinstall
Source: "..\release-assets\Blaze-signing.cer"; DestDir: "{tmp}"; DestName: "Blaze-signing.cer"; Flags: deleteafterinstall
Source: "Install-Blaze.ps1"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Run]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{tmp}\Install-Blaze.ps1"" -PackagePath ""{tmp}\Blaze.msix"" -CertificatePath ""{tmp}\Blaze-signing.cer"""; Flags: runhidden waituntilterminated; StatusMsg: "Installing Blaze..."
