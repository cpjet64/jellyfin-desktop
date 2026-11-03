function Find-OptionalTool {
    param([string]$Name, [string[]]$Candidates = @())
    foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    return $null
}

function Get-VerifiedDownload {
    param([string]$Url, [string]$Destination, [string]$Sha256)
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Destination -TimeoutSec 120
    if ((Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -ne $Sha256) {
        throw "SHA-256 mismatch for $Url"
    }
}

function Expand-ToolArchive {
    param([string]$Archive, [string]$Destination)
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & tar.exe -xf $Archive -C $Destination
    if ($LASTEXITCODE -ne 0) { throw "Archive extraction failed: $Archive" }
}

function Install-CiTools {
    param([string]$Directory, [hashtable]$Found)
    New-Item -ItemType Directory -Force -Path $Directory | Out-Null
    if (-not $Found.SevenZip) {
        $archive = Join-Path $Directory '7zip.7z'
        Get-VerifiedDownload 'https://github.com/ip7z/7zip/releases/download/26.00/7z2600-extra.7z' $archive '1CC38A9E3777CE0E4BBF84475672888A581D400633B0448FD973A7A6AA56CFDC'
        Expand-ToolArchive $archive "$Directory/7zip"
        $Found.SevenZip = Join-Path $Directory '7zip/x64/7za.exe'
    }
    if (-not $Found.CMake) {
        $archive = Join-Path $Directory 'cmake.zip'
        Get-VerifiedDownload 'https://cmake.org/files/v4.1/cmake-4.1.2-windows-x86_64.zip' $archive '82730741149681597510A4BC7095DA1460B472DAC24EAF9C23AD7440F47753AB'
        Expand-ToolArchive $archive "$Directory/cmake"
        $Found.CMake = Join-Path $Directory 'cmake/cmake-4.1.2-windows-x86_64/bin/cmake.exe'
    }
    if (-not $Found.Ninja) {
        $archive = Join-Path $Directory 'ninja.zip'
        Get-VerifiedDownload 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip' $archive '07FC8261B42B20E71D1720B39068C2E14FFCEE6396B76FB7A795FB460B78DC65'
        Expand-ToolArchive $archive "$Directory/ninja"
        $Found.Ninja = Join-Path $Directory 'ninja/ninja.exe'
    }
    if (-not $Found.Node) {
        $archive = Join-Path $Directory 'node.zip'
        Get-VerifiedDownload 'https://nodejs.org/dist/v22.21.1/node-v22.21.1-win-x64.zip' $archive '3C624E9FBE07E3217552EC52A0F84E2BDC2E6FFA7348F3FDFB9FBF8F42E23FCF'
        Expand-ToolArchive $archive "$Directory/node"
        $Found.Node = Join-Path $Directory 'node/node-v22.21.1-win-x64/node.exe'
    }
    if (-not $Found.Inno) { Install-CiInno $Directory $Found }
    foreach ($tool in @('SevenZip', 'CMake', 'Ninja', 'Node', 'Inno')) {
        if (-not (Test-Path -LiteralPath $Found[$tool] -PathType Leaf)) { throw "Missing $tool executable after bootstrap" }
        $env:PATH = "$(Split-Path $Found[$tool]);$env:PATH"
    }
}

function Install-CiInno {
    param([string]$Directory, [hashtable]$Found)
    $extractor = Join-Path $Directory 'innoextract.zip'
    $installer = Join-Path $Directory 'innosetup.exe'
    Get-VerifiedDownload 'https://github.com/UserUnknownFactor/innoextract_win/releases/download/670/innoextract670.zip' $extractor '79B69B9B1FCD98F42CCD4B245EFDF6A03BCFB674BA6AF482F5A46891C9ED4D14'
    Get-VerifiedDownload 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe' $installer '9C73C3BAE7ED48D44112A0F48E66742C00090BDB5BEF71D9D3C056C66E97B732'
    $signature = Get-AuthenticodeSignature -LiteralPath $installer
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Pyrsys B\.V\.') {
        throw 'Inno Setup signature verification failed'
    }
    Expand-ToolArchive $extractor "$Directory/innoextract"
    $output = Join-Path $Directory 'inno'
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    & "$Directory/innoextract/innoextract.exe" -d $output $installer
    if ($LASTEXITCODE -ne 0) { throw 'Inno Setup extraction failed' }
    $compiler = Get-ChildItem -LiteralPath $output -Filter ISCC.exe -Recurse | Select-Object -First 1
    if (-not $compiler -or (Get-FileHash -LiteralPath $compiler.FullName -Algorithm SHA256).Hash -ne '0A8757031B33777E4C9CBFFEE40F11A5062B36D25CBE144C1DB73B6102B80AD7') {
        throw 'Inno Setup compiler hash verification failed'
    }
    $Found.Inno = $compiler.FullName
}
