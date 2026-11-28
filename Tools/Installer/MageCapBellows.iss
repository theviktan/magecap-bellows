; The Windows setup program for MageCap Bellows (Inno Setup 6). CI builds it from the packaged
; editor folder:
;
;   iscc /DAppVersion=0.7.0 /DSourceDir=dist\MageCap-Bellows-0.7.0-Windows /DOutputDir=dist
;        /DOutputName=MageCap-Bellows-0.7.0-Windows-Setup Tools\Installer\MageCapBellows.iss
;
; It installs for the current user only (no administrator rights) under
; %LOCALAPPDATA%\Programs\MageCap Bellows, which the editor can update itself: Help > Check for
; Updates downloads the next setup program and runs it with /SILENT /UPDATE (the code section at
; the end waits for the editor to close). /NOLAUNCH leaves the editor closed afterwards (CI's test).

#ifndef AppVersion
  #error Pass /DAppVersion=<version>
#endif
#ifndef SourceDir
  #error Pass /DSourceDir=<the packaged editor folder>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef OutputName
  #define OutputName "MageCap-Bellows-" + AppVersion + "-Windows-Setup"
#endif

[Setup]
; The AppId ties every version together as one program: never change it.
AppId={{6C7C20F2-BD19-424A-9F88-D9C20EA9985E}
AppName=MageCap Bellows
AppVersion={#AppVersion}
AppVerName=MageCap Bellows {#AppVersion}
AppPublisher=MageCap Games
AppPublisherURL=https://magecapgames.com
AppCopyright=Copyright 2026 MageCap Games
VersionInfoVersion={#AppVersion}
VersionInfoCompany=MageCap Games
VersionInfoProductName=MageCap Bellows
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\MageCap Bellows
DefaultGroupName=MageCap Bellows
DisableProgramGroupPage=yes
DisableDirPage=auto
UsePreviousAppDir=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
SetupIconFile={#SourceDir}\Resources\Icons\Bellows.ico
UninstallDisplayIcon={app}\BellowsEditor.exe
UninstallDisplayName=MageCap Bellows
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
; A running editor is found by the mutex it holds (Platform::HoldRunningMarker); setup waits for it to close.
AppMutex=MageCapBellowsEditor
CloseApplications=no
LicenseFile={#SourceDir}\LICENSE

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "association"; Description: "Open .bproject files with MageCap Bellows"; GroupDescription: "Files:"

[InstallDelete]
; Folders the package replaces as a whole, so files a newer version dropped don't linger. Only over
; an installed Bellows, never in a folder that merely has the same names (Audit 21, A21-2).
Type: filesandordirs; Name: "{app}\Shaders"; Check: IsUpgrade
Type: filesandordirs; Name: "{app}\Resources"; Check: IsUpgrade
Type: filesandordirs; Name: "{app}\Python"; Check: IsUpgrade
Type: filesandordirs; Name: "{app}\PythonRuntime"; Check: IsUpgrade
Type: filesandordirs; Name: "{app}\Docs"; Check: IsUpgrade

[Files]
; The sample project is the user's to change: an update never overwrites it.
Source: "{#SourceDir}\Projects\*"; DestDir: "{app}\Projects"; Flags: recursesubdirs createallsubdirs onlyifdoesntexist
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "\Projects"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{userprograms}\MageCap Bellows"; Filename: "{app}\BellowsEditor.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\MageCap Bellows"; Filename: "{app}\BellowsEditor.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
; Per-user file association (HKCU\Software\Classes), only with the "association" task.
Root: HKCU; Subkey: "Software\Classes\.bproject"; ValueType: string; ValueName: ""; ValueData: "MageCapBellows.Project"; Flags: uninsdeletevalue; Tasks: association
Root: HKCU; Subkey: "Software\Classes\MageCapBellows.Project"; ValueType: string; ValueName: ""; ValueData: "MageCap Bellows Project"; Flags: uninsdeletekey; Tasks: association
Root: HKCU; Subkey: "Software\Classes\MageCapBellows.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\BellowsEditor.exe,0"; Tasks: association
Root: HKCU; Subkey: "Software\Classes\MageCapBellows.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\BellowsEditor.exe"" --project ""%1"""; Tasks: association

[Run]
; Opens the editor at the end, also after a silent update (no skipifsilent), so an update reopens Bellows.
Filename: "{app}\BellowsEditor.exe"; WorkingDir: "{app}"; Description: "Open MageCap Bellows"; Flags: nowait postinstall; Check: ShouldLaunch

[UninstallDelete]
Type: files; Name: "{app}\*.log"

[Code]
function HasSwitch(Name: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), Name) = 0 then
      Result := True;
end;

function IsUpgrade(): Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\BellowsEditor.exe'));
end;

function ShouldLaunch(): Boolean;
begin
  Result := not HasSwitch('/NOLAUNCH');
end;

// An update started from the editor (/UPDATE) begins while the editor is still closing: wait up to
// 30 seconds for it to let go of its mutex before AppMutex would stop the install.
function InitializeSetup(): Boolean;
var
  Tries: Integer;
begin
  if HasSwitch('/UPDATE') then
  begin
    Tries := 0;
    while CheckForMutexes('MageCapBellowsEditor') and (Tries < 60) do
    begin
      Sleep(500);
      Tries := Tries + 1;
    end;
  end;
  Result := True;
end;
