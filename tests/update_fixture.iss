#ifndef BuildRoot
#error BuildRoot is required
#endif
#ifndef OutputRoot
#error OutputRoot is required
#endif
[Setup]
AppId=KlipUpdaterIsolatedTest
AppName=Klip updater isolated test
AppVersion=9.0.0
DefaultDirName={#BuildRoot}
Uninstallable=no
CreateUninstallRegKey=no
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
OutputDir={#OutputRoot}
OutputBaseFilename=Klip-9.0.0-win64-setup
Compression=lzma2
SolidCompression=yes
[Files]
Source: "{#BuildRoot}\bin\64bit\Klip.exe"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#BuildRoot}\updated.marker"; DestDir: "{app}"; Flags: ignoreversion
#ifdef FailInstall
[Code]
function InitializeSetup(): Boolean;
begin
  Result := False;
end;
#endif
