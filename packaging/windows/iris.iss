; Inno Setup script for the TwentyWare Iris Windows installer.
;
; Build (from the repo root, with the built iris.exe available):
;   iscc /DMyAppVersion=1.2.3 /DSourceExe=build\iris.exe ^
;        /DLicensePath=LICENSE.txt /DOutputDir=dist packaging\windows\iris.iss
;
; All /D defines are optional and fall back to sensible local-build defaults.

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#ifndef SourceExe
  #define SourceExe "..\..\build\iris.exe"
#endif
#ifndef LicensePath
  #define LicensePath "..\..\LICENSE.txt"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

#define MyAppName "Iris"
#define MyAppPublisher "TwentyWare"
#define MyAppURL "https://github.com/twentyware/iris"
#define MyAppExeName "iris.exe"

[Setup]
; A stable AppId keeps upgrades and uninstalls working across versions.
AppId={{ED07B1B4-B830-4F46-8905-027709751882}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}/releases
; Per-user install so no admin rights / UAC prompt are needed for a tray utility.
PrivilegesRequired=lowest
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
LicenseFile={#LicensePath}
OutputDir={#OutputDir}
OutputBaseFilename=iris-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "startup"; Description: "Start {#MyAppName} automatically when I log in"; GroupDescription: "Startup:"

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; DestName: "{#MyAppExeName}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"

[Registry]
; Auto-start via the per-user Run key, only when the Startup task is selected.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; ValueName: "{#MyAppName}"; \
  ValueData: """{app}\{#MyAppExeName}"""; \
  Tasks: startup; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName} now"; \
  Flags: nowait postinstall skipifsilent
