function Get-MpvArchive {
    param([string]$Architecture, [string]$Version, [string]$Destination, [string]$ExpectedHash)
    $name = "mpv-dev-$Architecture-$Version.7z"
    $url = "https://downloads.sourceforge.net/project/mpv-player-windows/libmpv/$name"
    $stage = 'initial download'
    try {
        & curl.exe --location --fail --silent --show-error --connect-timeout 20 --max-time 120 --max-redirs 5 --output $Destination $url
        if ($LASTEXITCODE -ne 0) { throw 'Download failed' }
        $magic = [byte[]]@(0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C)
        $header = [byte[]]::new(6)
        $stream = [IO.File]::OpenRead($Destination)
        try { $count = $stream.Read($header, 0, 6) } finally { $stream.Dispose() }
        if ($count -ne 6 -or [BitConverter]::ToString($header) -ne [BitConverter]::ToString($magic)) {
            if ((Get-Item -LiteralPath $Destination).Length -gt 1MB) { throw 'Unexpected non-archive response' }
            $html = Get-Content -Raw -LiteralPath $Destination
            $match = [regex]::Match($html, '<meta http-equiv="refresh" content="\d+; url=([^"]+)"')
            if (-not $match.Success) { throw 'SourceForge did not return an archive or a recognized download redirect' }
            $redirect = [System.Net.WebUtility]::HtmlDecode($match.Groups[1].Value)
            $uri = [Uri]$redirect
            $expectedPath = "/project/mpv-player-windows/libmpv/$name"
            if ($uri.Scheme -ne 'https' -or $uri.Host -ne 'downloads.sourceforge.net' -or
                $uri.AbsolutePath -cne $expectedPath -or -not $uri.Query -or $uri.Fragment) {
                throw 'SourceForge returned an unexpected download redirect'
            }
            $stage = 'SourceForge-selected download'
            & curl.exe --location --fail --silent --show-error --connect-timeout 20 --max-time 120 --max-redirs 5 --output $Destination $redirect
            if ($LASTEXITCODE -ne 0) { throw 'Selected download failed' }
        }
        $stage = 'archive signature and checksum validation'
        $stream = [IO.File]::OpenRead($Destination)
        try { $count = $stream.Read($header, 0, 6) } finally { $stream.Dispose() }
        if ($count -ne 6 -or [BitConverter]::ToString($header) -ne [BitConverter]::ToString($magic)) {
            throw 'Download is not a 7-Zip archive'
        }
        if ((Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -ne $ExpectedHash) {
            throw 'Download SHA-256 does not match the pinned SourceForge checksum'
        }
    } catch {
        throw "Failed mpv $stage for $Architecture ($($_.Exception.GetType().Name)). Check SourceForge access and pinned checksum."
    }
}

function Install-Mpv {
    param([string]$Architecture, [string]$Destination, [string]$SevenZip, [string]$Workspace, [string]$Version, [string]$ExpectedHash)
    $archive = Join-Path $Workspace "mpv-$Architecture.7z"
    $extract = Join-Path $Workspace "extract-$Architecture"
    Get-MpvArchive $Architecture $Version $archive $ExpectedHash
    & $SevenZip x $archive "-o$extract" -y
    if ($LASTEXITCODE -ne 0) { throw "7-Zip failed to extract mpv for $Architecture ($LASTEXITCODE)" }
    $dll = Get-ChildItem -LiteralPath $extract -Recurse -Filter 'libmpv-2.dll' | Select-Object -First 1
    if (-not $dll) { throw "libmpv DLL missing in $archive" }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Copy-Item -LiteralPath $dll.FullName -Destination $Destination
    $include = Join-Path $dll.DirectoryName 'include'
    if ($Architecture -eq 'x86_64-v3') {
        if (-not (Test-Path -LiteralPath $include)) { throw 'mpv headers missing' }
        Copy-Item -LiteralPath $include -Destination $Destination -Recurse
    }
}
