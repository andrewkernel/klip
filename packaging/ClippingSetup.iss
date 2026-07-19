#define AppName "Klip"
#ifndef AppVersion
  #error AppVersion must be supplied by package-win64.ps1
#endif
#define AppPublisher "Andrewkernel"
#define AppUrl "https://github.com/andrewkernel/klip"
#define AppExeName "Klip.exe"
#ifndef BuildRoot
  #define BuildRoot "..\build\Release"
#endif
#ifndef OutputRoot
  #define OutputRoot "."
#endif

[Setup]
AppId={{8A44B102-4E2D-4701-B01E-0B43D5C6C5A8}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
VersionInfoCompany={#AppPublisher}
VersionInfoDescription=Klip game capture and replay recorder
VersionInfoCopyright=Copyright (C) 2026 Andrewkernel
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExeName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
OutputDir={#OutputRoot}
OutputBaseFilename=Klip-{#AppVersion}-win64-setup
PrivilegesRequired=lowest
MinVersion=10.0.18362
CloseApplications=yes
RestartApplications=no
AppMutex=Andrewkernel.Klip
LicenseFile={#BuildRoot}\EULA.txt
SetupIconFile=..\assets\klip.ico
WizardImageStretch=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Files]
Source: "{#BuildRoot}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
