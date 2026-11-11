#define AppName "WebKit Browser"
#ifndef AppVersion
  #define AppVersion "2026.11.0-port.1"
#endif
#ifndef Architecture
  #define Architecture "x64"
#endif
#ifndef SourceDir
  #define SourceDir "..\\publish\\win-x64"
#endif

[Setup]
AppId={{7D9E2E51-9AC2-49CE-9D3D-85A7F2A6F411}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=WebKit Browser contributors
DefaultDirName={localappdata}\Programs\WebKitBrowser
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir=..\installer-output
OutputBaseFilename=WebKitBrowser-{#AppVersion}-{#Architecture}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\WebKitBrowser.exe
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\WebKitBrowser.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\WebKitBrowser.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\WebKitBrowser.exe"; Description: "Launch {#AppName}"; Flags: postinstall nowait skipifsilent
