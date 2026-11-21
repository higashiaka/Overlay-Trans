# 공식 CLI 설치와 계정 연결을 담당한다.
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('codex', 'claude')]
    [string]$Provider,
    [string]$CliPath
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$Host.UI.RawUI.WindowTitle = "OverLay-Trans - $Provider 계정 연결"

function Refresh-Path {
    $env:PATH = @(
        [Environment]::GetEnvironmentVariable('Path', 'Machine'),
        [Environment]::GetEnvironmentVariable('Path', 'User'),
        $env:PATH,
        (Join-Path $env:USERPROFILE '.local\bin'),
        (Join-Path $env:APPDATA 'npm'),
        (Join-Path $env:ProgramFiles 'nodejs')
    ) -join ';'
}

try {
    Write-Host 'OverLay-Trans 계정 연결' -ForegroundColor Cyan
    Write-Host '브라우저에서 본인 계정으로 로그인해 주세요. 비밀번호나 토큰을 앱에 입력할 필요가 없습니다.'
    Refresh-Path
    if (-not $CliPath -or -not (Test-Path -LiteralPath $CliPath -PathType Leaf)) {
        $CliPath = ''
        $command = Get-Command "$Provider.exe", "$Provider.cmd" -CommandType Application -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($command) { $CliPath = $command.Source }
    }
    if (-not $CliPath) {
        if ($Provider -eq 'codex') {
            if (-not (Get-Command npm.cmd -ErrorAction SilentlyContinue)) {
                if (-not (Get-Command winget.exe -ErrorAction SilentlyContinue)) {
                    throw 'Node.js 설치가 필요합니다. https://nodejs.org 에서 LTS를 설치한 뒤 다시 연결해 주세요.'
                }
                Write-Host 'Codex CLI에 필요한 Node.js LTS를 설치합니다. 설치 확인 창이 뜨면 진행해 주세요.'
                & winget.exe install --exact --id OpenJS.NodeJS.LTS --accept-package-agreements --accept-source-agreements
                if ($LASTEXITCODE -ne 0) { throw 'Node.js 설치에 실패했습니다. 설치 후 다시 연결해 주세요.' }
                Refresh-Path
            }
            Write-Host '공식 Codex CLI를 설치합니다…'
            & npm.cmd install --global @openai/codex
            if ($LASTEXITCODE -ne 0) { throw 'Codex CLI 설치에 실패했습니다.' }
        } else {
            Write-Host '공식 Claude Code를 설치합니다…'
            $installer = Join-Path ([IO.Path]::GetTempPath()) ('overlay-claude-install-' + [guid]::NewGuid() + '.ps1')
            try {
                Invoke-WebRequest -UseBasicParsing -Uri 'https://claude.ai/install.ps1' -OutFile $installer
                & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $installer
                if ($LASTEXITCODE -ne 0) { throw 'Claude Code 설치에 실패했습니다.' }
            } finally {
                if (Test-Path -LiteralPath $installer) { Remove-Item -LiteralPath $installer }
            }
        }
        Refresh-Path
        $command = Get-Command "$Provider.exe", "$Provider.cmd" -CommandType Application -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if (-not $command) { throw 'CLI를 찾지 못했습니다. 앱을 다시 실행한 뒤 연결해 주세요.' }
        $CliPath = $command.Source
    }
    # ChatGPT는 앱에서 연결하고 Claude는 공식 CLI에서 로그인한다.
    if ($Provider -eq 'codex') {
        . (Join-Path $PSScriptRoot "chatgpt-sign-in.ps1")
        Connect-ChatGpt
        exit 0
    } else {
        & $CliPath auth login
    }
    if ($LASTEXITCODE -ne 0) { throw '로그인을 마치지 못했습니다. 다시 연결해 주세요.' }
    Write-Host ''
    Write-Host '로그인이 완료되었습니다. 확장 프로그램에서 연결 확인 후 설정 저장을 눌러 주세요.' -ForegroundColor Green
} catch {
    Write-Host ''
    if ($Provider -eq 'codex') { Write-Host 'ChatGPT 계정 연결에 실패했거나 취소되었습니다.' -ForegroundColor Red }
    else { Write-Host $_.Exception.Message -ForegroundColor Red }
    Read-Host 'Enter를 누르면 창을 닫습니다' | Out-Null
    exit 1
}
