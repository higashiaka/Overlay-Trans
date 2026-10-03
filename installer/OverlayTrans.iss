; OverLay-Trans 설치 프로그램 (Inno Setup 6.3 이상).
; installer/package.ps1로 build/package를 만든 뒤 컴파일한다.
;
;   ISCC.exe /DAppVersion=1.0.0 installer\OverlayTrans.iss
;
; 관리자 권한 없이 사용자 폴더에 설치한다. 모델(약 3GB)은 설치 프로그램에 넣지 않고,
; 앱을 처음 실행할 때 OverlayTrans.ps1이 내려받는다.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{6B0E3B0A-5B0F-4C0B-9A53-7C6D0F1E4A21}
AppName=OverLay-Trans
AppVersion={#AppVersion}
AppPublisher=higashiaka
AppPublisherURL=https://github.com/higashiaka/Overlay-Trans
DefaultDirName={autopf}\OverlayTrans
DefaultGroupName=OverLay-Trans
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\build\installer
OutputBaseFilename=OverlayTrans-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=OverLay-Trans

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
#if FileExists(CompilerPath + "Languages\Korean.isl")
Name: "ko"; MessagesFile: "compiler:Languages\Korean.isl"
#endif

[Files]
Source: "..\build\package\*"; DestDir: "{app}"; Excludes: "options.txt"; Flags: recursesubdirs ignoreversion
; 사용자가 고친 옵션은 새 버전을 설치해도 그대로 둔다.
Source: "..\build\package\options.txt"; DestDir: "{app}"; Flags: onlyifdoesntexist

[Dirs]
Name: "{app}\models"

[Icons]
; 기본 콘솔(conhost)로 실행한다. Windows 터미널에서 열리면 앱이 콘솔 창을 숨기지 못한다.
Name: "{autoprograms}\OverLay-Trans"; Filename: "{sys}\conhost.exe"; Parameters: "powershell.exe -NoProfile -ExecutionPolicy Bypass -File ""{app}\OverlayTrans.ps1"""; WorkingDir: "{app}"
Name: "{autodesktop}\OverLay-Trans"; Filename: "{sys}\conhost.exe"; Parameters: "powershell.exe -NoProfile -ExecutionPolicy Bypass -File ""{app}\OverlayTrans.ps1"""; WorkingDir: "{app}"

[Run]
Filename: "{sys}\conhost.exe"; Parameters: "powershell.exe -NoProfile -ExecutionPolicy Bypass -File ""{app}\OverlayTrans.ps1"""; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,OverLay-Trans}"; Flags: postinstall nowait skipifsilent

[UninstallDelete]
; 설치 후에 생긴 파일(내려받은 모델, 옵션, 페어링 키)도 함께 지운다.
Type: filesandordirs; Name: "{app}\models"
Type: files; Name: "{app}\options.txt"
Type: filesandordirs; Name: "{localappdata}\OverlayTrans"
