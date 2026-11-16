# 설치된 OverLay-Trans를 실행한다. 바탕 화면과 시작 메뉴의 바로 가기가 이 파일을 실행한다.
# 모델이 없으면 먼저 내려받고, options.txt에 적힌 옵션으로 앱을 시작한다.
param([switch]$PlanOnly)

$app = $PSScriptRoot
$models = Join-Path $app "models"
$exe = Join-Path $app "OverlayTransWin.exe"

# 앱의 기본 모델. 파일 이름은 앱(windows/main.cpp)의 기본값과 같아야 한다.
# 음성 인식 모델은 options.txt의 --stt-model에 따라 달라지므로 아래에서 따로 정한다.
$defaultSttModel = "ggml-large-v3-turbo-q5_0.bin"
$sttModelUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main"
$modelFiles = @(
    @{
        Name = "ggml-silero-v6.2.0.bin"
        Url  = "https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v6.2.0.bin"
        Note = "음성 구간 감지 (1MB)"
    },
    @{
        Name = "3dspeaker_speech_campplus_sv_zh_en_16k-common_advanced.onnx"
        Url  = "https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/3dspeaker_speech_campplus_sv_zh_en_16k-common_advanced.onnx"
        Note = "화자 구분 (30MB)"
    },
    @{
        Name = "gemma-3-4b-it-Q4_K_M.gguf"
        Url  = "https://huggingface.co/ggml-org/gemma-3-4b-it-GGUF/resolve/main/gemma-3-4b-it-Q4_K_M.gguf"
        Note = "번역 (2.5GB)"
    }
)

function Stop-WithMessage($message) {
    Write-Host ""
    Write-Host $message -ForegroundColor Red
    Read-Host "Enter를 누르면 창을 닫습니다" | Out-Null
    exit 1
}

# options.txt: 한 줄에 옵션 하나. "--이름 값" 형식이며 값에 공백이 있어도 따옴표 없이 적는다.
# 시작하면 이 창을 숨기고 알림 영역 아이콘으로 둔다. options.txt에 "--background off"를 적으면 창을 그대로 둔다.
$arguments = @("--background", "on")
$optionsFile = Join-Path $app "options.txt"
if (Test-Path $optionsFile) {
    foreach ($line in Get-Content -Encoding UTF8 $optionsFile) {
        $line = $line.Trim()
        if ($line -eq "" -or $line.StartsWith("#")) {
            continue
        }
        $name, $value = $line -split "\s+", 2
        $arguments += $name
        if ($value) {
            $arguments += $value
        }
    }
}

# Vulkan 패키지에서는 실제 그래픽 메모리에 맞춰 기본값을 고른다. 사용자가 options.txt에 적은 값이 우선이다.
$profileScript = Join-Path $app 'hardware-profile.ps1'
if (Test-Path $profileScript) {
    try {
        $profile = & $profileScript -Format Json | ConvertFrom-Json
        $probeText = & $exe --probe-hardware 2>$null | Select-Object -Last 1
        if ($LASTEXITCODE -eq 0 -and $probeText) {
            $detected = $probeText | ConvertFrom-Json
            $profile.vulkan = [bool]$detected.vulkan -and [long]$detected.free_vram_mb -ge 2048
            $profile.vram_mb = [long]$detected.free_vram_mb
        }
    } catch {
        $profile = [pscustomobject]@{ vulkan = $false; vram_mb = 0 }
    }
    $gpuOption = [array]::IndexOf($arguments, '--gpu')
    $gpuDisabled = $gpuOption -ge 0 -and $gpuOption + 1 -lt $arguments.Count -and
        $arguments[$gpuOption + 1] -eq 'cpu'
    if (-not $profile.vulkan -or $gpuDisabled) {
        if ($gpuOption -lt 0) { $arguments += @('--gpu', 'cpu') }
        if ([array]::IndexOf($arguments, '--stt-model') -lt 0) {
            $arguments += @('--stt-model', 'models\ggml-base.bin')
        }
        if ([array]::IndexOf($arguments, '--llm-gpu-layers') -lt 0) {
            $arguments += @('--llm-gpu-layers', '0')
        }
        Write-Host 'GPU를 사용할 수 없어 CPU 기본 설정을 적용합니다.'
    } else {
        $sttName = if ($profile.vram_mb -ge 6144) {
            'ggml-large-v3-turbo-q5_0.bin'
        } else {
            'ggml-small.bin'
        }
        $llmLayers = if ($profile.vram_mb -ge 7168) { 16 } elseif ($profile.vram_mb -ge 4096) { 8 } else { 0 }
        if ([array]::IndexOf($arguments, '--stt-model') -lt 0) {
            $arguments += @('--stt-model', "models\$sttName")
        }
        if ([array]::IndexOf($arguments, '--llm-gpu-layers') -lt 0) {
            $arguments += @('--llm-gpu-layers', [string]$llmLayers)
        }
        Write-Host "Vulkan GPU 감지: $($profile.vram_mb) MB, 기본 STT $sttName, 번역 GPU 층 $llmLayers"
    }
}

# CLI나 DeepL로 번역할 때는 기기 내 번역 모델을 내려받지 않는다.
# Windows 앱과 마찬가지로 저장된 설정이 options.txt보다 우선한다.
$translator = 'local'
$translatorOption = [array]::IndexOf($arguments, '--translator')
if ($translatorOption -ge 0 -and $translatorOption + 1 -lt $arguments.Count) {
    $translator = $arguments[$translatorOption + 1]
}
$settingsFile = Join-Path $env:LOCALAPPDATA 'OverlayTrans/translation-settings.json'
if (Test-Path -LiteralPath $settingsFile) {
    try {
        $savedSettings = Get-Content -LiteralPath $settingsFile -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($savedSettings.translator) { $translator = $savedSettings.translator }
    } catch {
        # 읽지 못하면 기본 모델을 준비한다.
    }
}
if ($translator -in @('codex', 'claude', 'deepl', 'codex-hybrid')) {
    $modelFiles = @($modelFiles | Where-Object { $_.Name -ne 'gemma-3-4b-it-Q4_K_M.gguf' })
}

# 음성 인식 모델. options.txt에 --stt-model이 있으면 그 파일을, 없으면 기본 모델을 쓴다.
# whisper.cpp가 배포하는 모델(ggml-*.bin)이면 없을 때 내려받는다. 그 밖의 파일은 사용자가 직접 넣어 둔 것으로 본다.
$sttPath = Join-Path $models $defaultSttModel
$sttOption = [array]::IndexOf($arguments, "--stt-model")
if ($sttOption -ge 0 -and $sttOption + 1 -lt $arguments.Count) {
    $sttPath = $arguments[$sttOption + 1]
    if (-not [System.IO.Path]::IsPathRooted($sttPath)) {
        $sttPath = Join-Path $app $sttPath
    }
}
$sttName = Split-Path -Leaf $sttPath
if ($sttName -match '^ggml-[\w.-]+\.bin$') {
    $modelFiles += @{
        Name   = $sttName
        Url    = "$sttModelUrl/$sttName"
        Note   = "음성 인식 ($sttName)"
        Target = $sttPath
    }
}

if ($PlanOnly) {
    [pscustomobject]@{
        arguments = @($arguments)
        models = @($modelFiles | ForEach-Object { $_.Name })
    } | ConvertTo-Json -Compress -Depth 4
    return
}

New-Item -ItemType Directory -Force $models | Out-Null
foreach ($model in $modelFiles) {
    $target = if ($model.Target) { $model.Target } else { Join-Path $models $model.Name }
    if (Test-Path $target) {
        continue
    }

    # 다 받기 전에는 .part 파일에 쓴다. 중간에 끊겨도 다음 실행 때 이어서 받는다.
    $partial = "$target.part"
    Write-Host "모델을 내려받습니다: $($model.Note)"
    & curl.exe --location --fail --retry 3 --continue-at - --output $partial $model.Url
    if ($LASTEXITCODE -ne 0) {
        Stop-WithMessage "모델을 내려받지 못했습니다. 인터넷 연결을 확인하고 다시 실행해 주세요."
    }
    Move-Item $partial $target
}

Write-Host "브라우저 확장 프로그램 폴더: $(Join-Path $app 'extension')"
Write-Host "앱이 시작되면 이 창은 숨겨지고 알림 영역(시계 옆)에 아이콘이 생깁니다."
Write-Host "아이콘을 오른쪽 클릭하면 페어링 코드 복사, 로그 보기, 종료를 할 수 있습니다."
Write-Host ""

Set-Location $app
& $exe @arguments
if ($LASTEXITCODE -ne 0) {
    Stop-WithMessage "앱이 오류로 종료되었습니다. 위의 메시지를 확인해 주세요."
}
