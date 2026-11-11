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
CloseApplications=yes

[Languages]
Name: "french";  MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

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

[InstallDelete]
; Drop files of the previous version that may no longer be shipped (old Qt
; plugins / QML modules, renamed ggml CPU variants).  User data lives in
; %APPDATA%\Hodeia and is never touched.
Type: filesandordirs; Name: "{app}\qml"
Type: files;          Name: "{app}\ggml-cpu-*.dll"

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
