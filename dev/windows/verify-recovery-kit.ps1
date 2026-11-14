param(
    [Parameter(Mandatory)][string]$SdkRoot,
    [Parameter(Mandatory)][string]$QtRoot
)
$ErrorActionPreference = 'Stop'
$python = $env:JELLYFIN_RECOVERY_PYTHON
$expected = $env:JELLYFIN_RECOVERY_PYTHON_SHA256
if (-not $python -or -not [IO.Path]::IsPathRooted($python) -or -not (Test-Path -LiteralPath $python -PathType Leaf)) {
    throw 'An absolute reviewed recovery Python executable is required'
}
if (-not $expected -or $expected -cnotmatch '^[0-9a-f]{64}$') {
    throw 'A reviewed lowercase recovery Python SHA-256 is required'
}
$item = Get-Item -LiteralPath $python -Force
if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Recovery Python must not be a reparse point' }
if ((Get-FileHash -LiteralPath $python -Algorithm SHA256).Hash.ToLowerInvariant() -cne $expected) {
    throw 'Recovery Python executable differs from its reviewed identity'
}
$verifier = Join-Path $PSScriptRoot '../../patches/mpv/build/recovery_artifacts.py'
& $python -I -B $verifier verify --lock $env:JELLYFIN_RECOVERY_LOCK `
    --approved-lock-sha256 $env:JELLYFIN_RECOVERY_LOCK_SHA256 --root $SdkRoot --qt-root $QtRoot `
    --manifest $env:JELLYFIN_RECOVERY_MANIFEST --approved-manifest-sha256 $env:JELLYFIN_RECOVERY_MANIFEST_SHA256 `
    --dumpbin $env:JELLYFIN_RECOVERY_DUMPBIN
if ($LASTEXITCODE -ne 0) { throw 'Custom recovery kit verification failed' }
