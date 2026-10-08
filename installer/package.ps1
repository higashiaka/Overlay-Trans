# 설치할 파일을 한 폴더에 모으고 zip 하나로 묶는다. 빌드를 마친 뒤 저장소 루트에서 실행한다.
#
#   powershell -ExecutionPolicy Bypass -File installer/package.ps1
#   powershell -ExecutionPolicy Bypass -File installer/package.ps1 -BuildDir build/arm64 -Arch arm64 -Backend cpu
#
# 결과는 build/installer/OverlayTrans-<버전>-<아키텍처>-<백엔드>.zip 이다. 설치 프로그램에는 넣지 않고 릴리즈에
# 따로 올린다. 설치 프로그램(installer/OverlayTrans.iss)이 PC에 맞는 것을 골라 내려받는다.
# 마지막 줄로 프로젝트 버전을 출력한다.
param(
    [string]$BuildDir = "build/vk",
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",
    [ValidateSet("vulkan", "cpu", "opencl")]
    [string]$Backend = "vulkan"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# Visual Studio 생성기는 windows/Release 아래에, Ninja(ARM64 프리셋)는 windows 바로 아래에 실행 파일을 만든다.
$exe = @("windows/Release", "windows") |
    ForEach-Object { Join-Path $root "$BuildDir/$_/OverlayTransWin.exe" } |
    Where-Object { Test-Path $_ } |
    Select-Object -First 1
if (-not $exe) {
    throw "Build output not found under: $BuildDir/windows"
}

$out = Join-Path $root "build/package/$Arch-$Backend"
if (Test-Path $out) {
    Remove-Item -Recurse -Force $out
}
New-Item -ItemType Directory -Force $out | Out-Null

Copy-Item $exe $out
# 화자 구분(sherpa-onnx)과 ONNX Runtime DLL. 빌드할 때 실행 파일 옆에 복사되어 있다.
foreach ($dll in @("sherpa-onnx-c-api.dll", "onnxruntime.dll", "onnxruntime_providers_shared.dll")) {
    Copy-Item (Join-Path (Split-Path -Parent $exe) $dll) $out
}
# OpenCL 로더. OpenCL 프리셋으로 빌드한 경우에만 있다.
$openclDll = Join-Path (Split-Path -Parent $exe) "OpenCL.dll"
$hasOpenCl = Test-Path $openclDll
if ($hasOpenCl) {
    Copy-Item $openclDll $out
}
Copy-Item (Join-Path $PSScriptRoot "OverlayTrans.ps1") $out
# 기본 옵션은 아키텍처마다 다르다. (ARM64는 GPU 옵션이 없고 음성 인식 모델이 작다.)
$options = if ($Arch -eq "arm64") { "options-arm64.txt" } else { "options.txt" }
Copy-Item (Join-Path $PSScriptRoot $options) (Join-Path $out "options.txt")
Copy-Item -Recurse (Join-Path $root "extension") (Join-Path $out "extension")

# Visual C++ 런타임을 실행 파일 옆에 둔다. 따로 설치하지 않아도(관리자 권한 없이) 실행되게 하기 위해서다.
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
$redist = Get-ChildItem (Join-Path $vs "VC/Redist/MSVC") -Directory |
    Where-Object { $_.Name -match '^\d' } |
    Sort-Object { [version]$_.Name } |
    Select-Object -Last 1
$runtime = @(
    "Microsoft.VC143.CRT/msvcp140.dll",
    "Microsoft.VC143.CRT/msvcp140_atomic_wait.dll",
    "Microsoft.VC143.CRT/vcruntime140.dll",
    "Microsoft.VC143.CRT/vcruntime140_1.dll"
)
# ARM64 빌드(Clang)는 OpenMP를 쓰지 않는다.
if ($Arch -eq "x64") {
    $runtime += "Microsoft.VC143.OpenMP/vcomp140.dll"
}
foreach ($dll in $runtime) {
    Copy-Item (Join-Path $redist.FullName "$Arch/$dll") $out
}

# 함께 배포하는 코드의 라이선스.
$licenses = Join-Path $out "licenses"
New-Item -ItemType Directory -Force $licenses | Out-Null
$licenseFiles = @{
    "LICENSE"                                          = "OverLay-Trans.txt"
    "third_party/llama.cpp/LICENSE"                    = "llama.cpp.txt"
    "third_party/whisper.cpp/LICENSE"                  = "whisper.cpp.txt"
    "third_party/miniaudio/LICENSE"                    = "miniaudio.txt"
    "third_party/llama.cpp/vendor/cpp-httplib/LICENSE" = "cpp-httplib.txt"
    "third_party/llama.cpp/licenses/LICENSE-jsonhpp"   = "nlohmann-json.txt"
    "third_party/licenses/sherpa-onnx.txt"             = "sherpa-onnx.txt"
    "third_party/licenses/onnxruntime.txt"             = "onnxruntime.txt"
}
if ($hasOpenCl) {
    $licenseFiles["$BuildDir/_deps/opencl_icd_loader-src/LICENSE"] = "OpenCL-ICD-Loader.txt"
}
foreach ($source in $licenseFiles.Keys) {
    Copy-Item (Join-Path $root $source) (Join-Path $licenses $licenseFiles[$source])
}

# 설치 프로그램에 적을 버전은 CMakeLists.txt의 프로젝트 버전을 따른다.
$cmake = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($cmake -notmatch 'project\(OverlayTrans VERSION (\d+\.\d+\.\d+)') {
    throw "Project version not found in CMakeLists.txt"
}
$version = $Matches[1]

$zipFolder = Join-Path $root "build/installer"
New-Item -ItemType Directory -Force $zipFolder | Out-Null
$zip = Join-Path $zipFolder "OverlayTrans-$version-$Arch-$Backend.zip"
if (Test-Path $zip) {
    Remove-Item -Force $zip
}
Compress-Archive -Path (Join-Path $out "*") -DestinationPath $zip

Write-Host "Packaged OverLay-Trans $version into $zip"
Write-Output $version
