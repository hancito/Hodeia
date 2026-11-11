; Hodeia — Inno Setup script (Inno Setup 6.5.2+ / 7).
;
; Packages the deployed Release build (build\Release: Hodeia.exe, Qt runtime
; from windeployqt, llama.cpp/CUDA/Vulkan/OCR DLLs, themes, fonts, tessdata,
; i18n, bundled models) into a single Hodeia-Setup-<version>-x64.exe.
;
; Normally built by scripts\Windows\package-app.ps1, which passes:
;   /DAppVersion=<x.y.z>        from project(VERSION) in CMakeLists.txt
;   /DBuildDir=<path>           deployed build folder (default build\Release)
;   /DVCRedistBuild=<n>         build number of the bundled vc_redist.x64.exe
; Manual build from the repo root:
;   iscc package\windows\hodeia.iss

#ifndef AppVersion
  #define AppVersion "0.1"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build\Release"
#endif
#ifndef VCRedistBuild
  #define VCRedistBuild "0"
#endif

#define AppName "Hodeia"
#define AppExe  "Hodeia.exe"

[Setup]
; AppId must NEVER change: it is how upgrades and the uninstaller find a
; previous install.
;
; Updates: bump project(VERSION) in CMakeLists.txt, rebuild, re-package and
; ship the new Setup.exe.  Run over an existing install, it:
;   - reuses the previous install dir and tasks (same AppId),
;   - closes a running Hodeia (CloseApplications),
;   - asks before installing an OLDER version over a newer one,
;   - silently uninstalls the previous version first, so files that are no
;     longer shipped (old Qt plugins, ggml CPU variants...) do not linger.
; User data (%APPDATA%\Hodeia: projects, models, config) is never touched.
AppId={{38F668CB-CEA6-49F6-B83A-DB655A80D71F}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppName}
VersionInfoVersion={#AppVersion}
LicenseFile=..\..\LICENSE
SetupIconFile=..\..\Resources\hodeia.ico
UninstallDisplayIcon={app}\{#AppExe}

; Per-user install by default (no admin prompt, %LOCALAPPDATA%\Programs);
; the user may pick an all-users install (Program Files) in a dialog.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes

; x64-only binaries.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

OutputDir=..\..\dist
OutputBaseFilename=Hodeia-Setup-{#AppVersion}-x64

; ~3 GB payload, mostly already-compressed .gguf models and CUDA DLLs:
; non-solid LZMA2 keeps compile time and memory reasonable.  A single
; Setup.exe can hold up to ~4.2 GB of compressed data (Inno Setup >= 6.5.2).
Compression=lzma2/normal
SolidCompression=no
LZMAUseSeparateProcess=yes
WizardStyle=modern
; Update behaviour (see the [Code] section for downgrade check + cleanup).
CloseApplications=force
RestartApplications=no
UsePreviousAppDir=yes
UsePreviousTasks=yes

[Languages]
Name: "french";  MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
french.DowngradeWarning=Hodeia %1 est déjà installé, ce qui est plus récent que la version %2 de cet installeur.%n%nInstaller quand même l'ancienne version ?
english.DowngradeWarning=Hodeia %1 is already installed, which is newer than this installer's version %2.%n%nInstall the older version anyway?

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The whole deploy folder, minus the CMake/Ninja build artefacts that live in
; the same directory.  A leading "\" anchors a pattern to the folder root.
Source: "{#BuildDir}\*"; DestDir: "{app}"; \
  Excludes: "\CMakeFiles,\src,\vcpkg_installed,\Hodeia_autogen,\HodeiaEngine_autogen,\Hodeia_app,\.qt,\.rcc,\.lupdate,\.ninja_deps,\.ninja_log,\build.ninja,\CMakeCache.txt,\cmake_install.cmake,\compile_commands.json,\vcpkg-manifest-install.log,\Hodeia.log,\vc_redist.x64.exe,*.lib,*.exp,*.pdb,*.ilk"; \
  Flags: ignoreversion recursesubdirs createallsubdirs

; MSVC runtime (staged by windeployqt); installed only when missing/older.
Source: "{#BuildDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; \
  Flags: deleteafterinstall; Check: VCRedistNeedsInstall

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}";  Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; vc_redist requires admin: shellexec lets Windows show the UAC prompt even
; from a per-user (non-elevated) install.
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; \
  StatusMsg: "Microsoft Visual C++ Runtime..."; \
  Flags: shellexec waituntilterminated; Check: VCRedistNeedsInstall
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
  Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\Hodeia.log"

[Code]
// True when the x64 VC++ 2015-2022+ runtime is absent or older than the one
// bundled with this build.
function VCRedistNeedsInstall: Boolean;
var
  Installed, Bld: Cardinal;
begin
  Result := True;
  if RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
                        'Installed', Installed) and (Installed = 1) and
     RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
                        'Bld', Bld) then
    Result := Bld < {#VCRedistBuild};
end;

// ---------------------------------------------------------------------------
// Updates over a previous install
// ---------------------------------------------------------------------------
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{38F668CB-CEA6-49F6-B83A-DB655A80D71F}_is1';

// Uninstall entry of a previous install in the current install mode
// (HKLM for an all-users/admin install, HKCU for a per-user one).
function GetPreviousInstall(var UninstallString, Version: String): Boolean;
var
  Root: Integer;
begin
  if IsAdminInstallMode then Root := HKLM64 else Root := HKCU;
  Result := RegQueryStringValue(Root, UninstallKey, 'UninstallString', UninstallString);
  if Result and not RegQueryStringValue(Root, UninstallKey, 'DisplayVersion', Version) then
    Version := '';
end;

// Numeric dotted-version compare ("0.10" > "0.9"): < 0, 0 or > 0.
function CompareVersions(A, B: String): Integer;
var
  PA, PB: Integer;
begin
  Result := 0;
  while (Result = 0) and ((A <> '') or (B <> '')) do
  begin
    PA := Pos('.', A); if PA = 0 then PA := Length(A) + 1;
    PB := Pos('.', B); if PB = 0 then PB := Length(B) + 1;
    Result := StrToIntDef(Copy(A, 1, PA - 1), 0) - StrToIntDef(Copy(B, 1, PB - 1), 0);
    Delete(A, 1, PA);
    Delete(B, 1, PB);
  end;
end;

// No silent downgrade: ask first (answer defaults to No in /SILENT mode).
function InitializeSetup: Boolean;
var
  Uninst, Prev: String;
begin
  Result := True;
  if GetPreviousInstall(Uninst, Prev) and (Prev <> '') and
     (CompareVersions(Prev, '{#AppVersion}') > 0) then
    Result := SuppressibleMsgBox(
      FmtMessage(CustomMessage('DowngradeWarning'), [Prev, '{#AppVersion}']),
      mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES;
end;

// Remove the previous version with its own uninstaller before copying the
// new files.  It works from its uninstall log, so it only deletes files it
// installed itself (never user data or foreign files in the folder).
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Uninst, Prev: String;
  ResultCode: Integer;
begin
  Result := '';
  if GetPreviousInstall(Uninst, Prev) then
    if not Exec(RemoveQuotes(Uninst), '/VERYSILENT /NORESTART /SUPPRESSMSGBOXES',
                '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
      // (Keep the [ResultCode] array on this line: a [Code] line that starts
      // with "[" is parsed by Inno Setup as a new section tag.)
      Log(Format('Previous version uninstall failed (code %d); installing over it.', [ResultCode]));
end;
