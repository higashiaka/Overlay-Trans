# 설치된 OverLay-Trans를 실행한다. 바탕 화면과 시작 메뉴의 바로 가기가 이 파일을 실행한다.
# 모델이 없으면 먼저 내려받고, options.txt에 적힌 옵션으로 앱을 시작한다.

$app = $PSScriptRoot
$models = Join-Path $app "models"
$exe = Join-Path $app "OverlayTransWin.exe"

# 앱의 기본 모델. 파일 이름은 앱(windows/main.cpp)의 기본값과 같아야 한다.
$modelFiles = @(
    @{
        Name = "ggml-silero-v6.2.0.bin"
        Url  = "https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v6.2.0.bin"
        Note = "음성 구간 감지 (1MB)"
    },
    @{
        Name = "ggml-large-v3-turbo-q5_0.bin"
        Url  = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q5_0.bin"
        Note = "음성 인식 (0.6GB)"
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

New-Item -ItemType Directory -Force $models | Out-Null
foreach ($model in $modelFiles) {
    $target = Join-Path $models $model.Name
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

# options.txt: 한 줄에 옵션 하나. "--이름 값" 형식이며 값에 공백이 있어도 따옴표 없이 적는다.
$arguments = @()
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

Write-Host "브라우저 확장 프로그램 폴더: $(Join-Path $app 'extension')"
Write-Host "아래에 나오는 Pairing code를 확장 프로그램에 입력하세요. 종료하려면 Enter를 누릅니다."
Write-Host ""

Set-Location $app
& $exe @arguments
if ($LASTEXITCODE -ne 0) {
    Stop-WithMessage "앱이 오류로 종료되었습니다. 위의 메시지를 확인해 주세요."
}
