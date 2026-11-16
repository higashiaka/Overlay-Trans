param(
    [ValidateSet('Json', 'Package')]
    [string]$Format = 'Json',
    [string]$OutputPath,
    # These overrides make the selection logic testable without changing the host PC.
    [ValidateSet('auto', 'yes', 'no')]
    [string]$Runtime = 'auto',
    [int]$VramMb = -1
)

$ErrorActionPreference = 'Stop'

function Get-DisplayMemoryMb {
    $largest = 0L
    $videoKeys = Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Video' -ErrorAction SilentlyContinue
    foreach ($videoKey in $videoKeys) {
        $adapter = Get-ItemProperty (Join-Path $videoKey.PSPath '0000') -ErrorAction SilentlyContinue
        if (-not $adapter -or -not $adapter.DriverDesc) { continue }
        $value = $adapter.'HardwareInformation.qwMemorySize'
        if ($value -is [byte[]] -and $value.Length -ge 8) {
            $bytes = [BitConverter]::ToUInt64($value, 0)
        } elseif ($null -ne $value) {
            $bytes = [uint64]$value
        } else {
            continue
        }
        $largest = [Math]::Max($largest, [long]($bytes / 1MB))
    }

    if ($largest -eq 0) {
        try {
            foreach ($adapter in @(Get-CimInstance Win32_VideoController -ErrorAction Stop)) {
                if ($adapter.AdapterRAM) {
                    $largest = [Math]::Max($largest, [long]([uint64]$adapter.AdapterRAM / 1MB))
                }
            }
        } catch {
            # An unavailable hardware query selects the CPU package safely.
        }
    }
    return $largest
}

$runtimePresent = switch ($Runtime) {
    'yes' { $true }
    'no'  { $false }
    default { Test-Path (Join-Path $env:WINDIR 'System32\vulkan-1.dll') }
}
$memoryMb = if ($VramMb -ge 0) { [long]$VramMb } else { Get-DisplayMemoryMb }
$useVulkan = $runtimePresent -and $memoryMb -ge 2048
$profile = [pscustomobject]@{
    vulkan = [bool]$useVulkan
    vram_mb = [long]$memoryMb
}
$result = if ($Format -eq 'Package') {
    if ($useVulkan) { 'x64-vulkan' } else { 'x64-cpu' }
} else {
    ConvertTo-Json -InputObject $profile -Compress
}

if ($OutputPath) {
    [System.IO.File]::WriteAllText($OutputPath, $result, [System.Text.Encoding]::ASCII)
} else {
    Write-Output $result
}
