# 설치 프로그램을 만든다. installer/package.ps1로 패키지 zip을 만든 뒤 저장소 루트에서 실행한다.
#
#   powershell -ExecutionPolicy Bypass -File installer/make-installer.ps1
#
# build/installer에 있는 패키지 zip의 SHA-256을 설치 프로그램에 넣는다. 설치 프로그램은 내려받은 패키지가
# 이 값과 다르면 설치하지 않는다. zip이 없는 프로세서는 설치 프로그램이 지원하지 않는 것으로 안내한다.
# 결과: build/installer/OverlayTrans-Setup-<버전>.exe (Inno Setup 6 필요)
param(
    [string]$Iscc
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# Inno Setup은 모든 사용자용으로 설치하면 Program Files에, winget처럼 현재 사용자용으로 설치하면 사용자 폴더에 들어간다.
if (-not $Iscc) {
    $Iscc = @(${env:ProgramFiles(x86)}, (Join-Path $env:LOCALAPPDATA "Programs")) |
        ForEach-Object { Join-Path $_ "Inno Setup 6/ISCC.exe" } |
        Where-Object { Test-Path $_ } |
        Select-Object -First 1
    if (-not $Iscc) {
        throw "ISCC.exe not found. Install Inno Setup 6 or pass -Iscc <path>."
    }
}

$cmake = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($cmake -notmatch 'project\(OverlayTrans VERSION (\d+\.\d+\.\d+)') {
    throw "Project version not found in CMakeLists.txt"
}
$version = $Matches[1]

# 설치 프로그램(installer/OverlayTrans.iss)이 아는 패키지와, 그 SHA-256을 넘길 이름.
$packages = @{
    "x64-vulkan" = "Sha256X64Vulkan"
    "arm64-cpu"  = "Sha256Arm64Cpu"
}
$defines = @("/DAppVersion=$version")
foreach ($name in $packages.Keys) {
    $zip = Join-Path $root "build/installer/OverlayTrans-$version-$name.zip"
    if (Test-Path $zip) {
        $hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
        $defines += "/D$($packages[$name])=$hash"
        Write-Host "$name : $hash"
    } else {
        Write-Host "$name : (패키지 없음)"
    }
}

& $Iscc @defines (Join-Path $PSScriptRoot "OverlayTrans.iss")
if ($LASTEXITCODE -ne 0) {
    throw "ISCC failed with exit code $LASTEXITCODE"
}
