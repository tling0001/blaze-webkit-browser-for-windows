#define AppVersion GetEnv("BLAZE_VERSION")
#ifndef AppVersion
  #error BLAZE_VERSION must be set by the release workflow.
#endif

[Setup]
AppId={{44C86CE5-2F2D-412F-9565-A731B6070756}
AppName=Blaze
AppVersion={#AppVersion}
AppPublisher=Blaze contributors
DefaultDirName={localappdata}\Programs\Blaze
DefaultGroupName=Blaze
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64
OutputDir=..\release-assets
OutputBaseFilename=Blaze-Setup-{#AppVersion}-x64
SetupIconFile=..\Assets\AppIcon.ico
Uninstallable=yes
UninstallDisplayIcon={app}\Blaze.exe
DisableProgramGroupPage=yes
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Files]
Source: "..\publish\win-x64\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Blaze"; Filename: "{app}\Blaze.exe"
Name: "{autodesktop}\Blaze"; Filename: "{app}\Blaze.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Blaze.exe"; Description: "Launch Blaze"; Flags: postinstall nowait skipifsilent
