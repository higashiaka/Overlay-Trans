; OverLay-Trans 설치 프로그램 (Inno Setup 6.3 이상).
; installer/make-installer.ps1이 컴파일한다.
;
; 앱은 설치 프로그램에 들어 있지 않다. PC의 프로세서를 확인해 맞는 패키지(installer/package.ps1이 만든 zip)를
; 릴리즈에서 내려받아 설치한다. 그래서 설치 프로그램은 하나만 배포한다.
;
;   x64   -> x64-vulkan  (Vulkan은 AMD, NVIDIA, Intel GPU에서 모두 동작한다)
;   ARM64 -> arm64-cpu   (스냅드래곤 X. Adreno GPU는 CPU보다 느려서 쓰지 않는다)
;
; 관리자 권한 없이 사용자 폴더에 설치한다. 모델은 앱을 처음 실행할 때 OverlayTrans.ps1이 내려받는다.
;
; 릴리즈가 공개되기 전에 시험할 때는 내려받는 대신 로컬 zip을 지정한다.
;
;   OverlayTrans-Setup-1.0.0.exe /PACKAGE=C:\path\OverlayTrans-1.0.0-arm64-cpu.zip

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
; 패키지 zip의 SHA-256. 비어 있으면 그 프로세서용 패키지 없이 만든 설치 프로그램이다.
#ifndef Sha256X64Vulkan
  #define Sha256X64Vulkan ""
#endif
#ifndef Sha256Arm64Cpu
  #define Sha256Arm64Cpu ""
#endif
#define PackageBaseUrl "https://github.com/higashiaka/Overlay-Trans/releases/download/v" + AppVersion

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
; ARM64 Windows 11도 x64compatible에 들어간다.
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

[CustomMessages]
en.NoPackage=This installer does not include a package for this PC's processor.
en.ExtractFailed=Failed to extract the downloaded package.
#if FileExists(CompilerPath + "Languages\Korean.isl")
ko.NoPackage=이 설치 프로그램에는 이 PC의 프로세서에 맞는 패키지가 없습니다.
ko.ExtractFailed=내려받은 패키지의 압축을 풀지 못했습니다.
#endif

[Dirs]
Name: "{app}\models"

[Icons]
; 기본 콘솔(conhost)로 실행한다. Windows 터미널에서 열리면 앱이 콘솔 창을 숨기지 못한다.
Name: "{autoprograms}\OverLay-Trans"; Filename: "{sys}\conhost.exe"; Parameters: "powershell.exe -NoProfile -ExecutionPolicy Bypass -File ""{app}\OverlayTrans.ps1"""; WorkingDir: "{app}"
Name: "{autodesktop}\OverLay-Trans"; Filename: "{sys}\conhost.exe"; Parameters: "powershell.exe -NoProfile -ExecutionPolicy Bypass -File ""{app}\OverlayTrans.ps1"""; WorkingDir: "{app}"

[Run]
Filename: "{sys}\conhost.exe"; Parameters: "powershell.exe -NoProfile -ExecutionPolicy Bypass -File ""{app}\OverlayTrans.ps1"""; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,OverLay-Trans}"; Flags: postinstall nowait skipifsilent

[UninstallDelete]
; 앱 파일은 내려받아 풀어 놓은 것이라 설치 프로그램이 목록을 모른다. 설치 폴더를 통째로 지운다.
; 설치 후에 생긴 파일(내려받은 모델, 옵션)도 함께 지워진다.
Type: filesandordirs; Name: "{app}"
Type: filesandordirs; Name: "{localappdata}\OverlayTrans"

[Code]
var
  DownloadPage: TDownloadWizardPage;
  PackageFile: String;

// 이 PC에 설치할 패키지의 이름과 SHA-256을 정한다. 지원하는 패키지가 늘어나면 여기에 조건을 더한다.
procedure SelectPackage(var Name, Sha256: String);
begin
  if IsArm64 then begin
    Name := 'arm64-cpu';
    Sha256 := '{#Sha256Arm64Cpu}';
  end else begin
    Name := 'x64-vulkan';
    Sha256 := '{#Sha256X64Vulkan}';
  end;
end;

procedure InitializeWizard;
begin
  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), nil);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Name, Sha256, FileName: String;
begin
  Result := True;
  if CurPageID <> wpReady then
    exit;

  // 시험용: 내려받지 않고 지정한 zip을 설치한다.
  PackageFile := ExpandConstant('{param:PACKAGE}');
  if PackageFile <> '' then
    exit;

  SelectPackage(Name, Sha256);
  if Sha256 = '' then begin
    SuppressibleMsgBox(CustomMessage('NoPackage'), mbCriticalError, MB_OK, IDOK);
    Result := False;
    exit;
  end;

  FileName := 'OverlayTrans-{#AppVersion}-' + Name + '.zip';
  DownloadPage.Clear;
  DownloadPage.Add('{#PackageBaseUrl}/' + FileName, FileName, Sha256);
  DownloadPage.Show;
  try
    try
      DownloadPage.Download;
      PackageFile := ExpandConstant('{tmp}\') + FileName;
    except
      if not DownloadPage.AbortedByUser then
        SuppressibleMsgBox(AddPeriod(GetExceptionMessage), mbCriticalError, MB_OK, IDOK);
      Result := False;
    end;
  finally
    DownloadPage.Hide;
  end;
end;

// 패키지를 설치 폴더에 푼다. Windows 10 이상에 들어 있는 tar로 zip을 푼다.
procedure CurStepChanged(CurStep: TSetupStep);
var
  Arguments: String;
  ExitCode: Integer;
begin
  if CurStep <> ssInstall then
    exit;

  ForceDirectories(ExpandConstant('{app}'));
  Arguments := '-xf "' + PackageFile + '" -C "' + ExpandConstant('{app}') + '"';
  // 사용자가 고친 옵션은 새 버전을 설치해도 그대로 둔다.
  if FileExists(ExpandConstant('{app}\options.txt')) then
    Arguments := Arguments + ' --exclude options.txt';

  if not Exec(ExpandConstant('{sys}\tar.exe'), Arguments, '', SW_HIDE, ewWaitUntilTerminated, ExitCode) or
     (ExitCode <> 0) then
    RaiseException(CustomMessage('ExtractFailed'));
end;
