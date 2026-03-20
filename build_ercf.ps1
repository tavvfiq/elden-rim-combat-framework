param(
    [string]$CommonLibSSEDir = "D:\Modding\CommonLibSSE-NG",
    [string]$Target = "ercf",
    [switch]$WithTests
)

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

if ([string]::IsNullOrWhiteSpace($CommonLibSSEDir)) {
    $commonlibDefault = Join-Path $repoRoot "..\CommonLibSSE-NG"
    $CommonLibSSEDir = $commonlibDefault
}

if (-not (Test-Path $CommonLibSSEDir)) {
    Write-Error "CommonLibSSE-NG not found: '$CommonLibSSEDir'"
    Write-Output "Provide it explicitly, for example:"
    Write-Output "  .\build_ercf.ps1 -CommonLibSSEDir 'D:\Modding\CommonLibSSE-NG' -WithTests"
    exit 1
}

Set-Location $repoRoot

Write-Output "Configuring xmake with CommonLibSSE-NG: $CommonLibSSEDir"
& xmake f --commonlibsse_dir="$CommonLibSSEDir"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Output "Building target: $Target"
& xmake build $Target
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($WithTests) {
    Write-Output "Building tests target: combat_math_tests"
    & xmake build combat_math_tests
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

