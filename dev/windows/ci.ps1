$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
Set-Location $root
$build = Join-Path $root 'build'
$deps = Join-Path $root 'deps'
$ci = Join-Path $deps "ci-$env:CI_JOB_ID"
$results = Join-Path $build 'ci-results'
$qtVersion = '6.9.3'
$mpvVersion = '20261018-git-01e2533f31'
. "$PSScriptRoot/ci-tools.ps1"
. "$PSScriptRoot/ci-mpv.ps1"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Run-Native {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

if (-not $env:CI_JOB_ID) { throw 'This script requires an isolated GitLab job checkout.' }
$drive = Get-PSDrive -Name ([System.IO.Path]::GetPathRoot($root).Substring(0, 1))
if ($drive.Free -lt 20GB) { throw 'At least 20 GiB free space is required before dependency downloads.' }
$python = Find-OptionalTool 'python.exe'
if (-not $python) { throw 'Python is required on the runner' }
if (-not (Find-OptionalTool 'curl.exe')) { throw 'Windows curl.exe is required for mpv downloads' }
$found = @{
    SevenZip = Find-OptionalTool '7z.exe' @("$env:ProgramFiles/7-Zip/7z.exe")
    CMake = Find-OptionalTool 'cmake.exe'
    Ninja = Find-OptionalTool 'ninja.exe'
    Node = Find-OptionalTool 'node.exe'
    Inno = Find-OptionalTool 'ISCC.exe' @("${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe", "$env:ProgramFiles/Inno Setup 6/ISCC.exe")
}
if ($found.Inno) {
    # ISCC's file-version metadata can be 0.0.0.0. Reuse only the tested compiler.
    if ((Get-FileHash -LiteralPath $found.Inno -Algorithm SHA256).Hash -ne '0A8757031B33777E4C9CBFFEE40F11A5062B36D25CBE144C1DB73B6102B80AD7') {
        $found.Inno = $null
    }
}
if ($found.Node) {
    $nodeVersion = & $found.Node --version
    if ($LASTEXITCODE -ne 0 -or $nodeVersion -notmatch '^v(\d+)\.' -or [int]$Matches[1] -lt 20) {
        $found.Node = $null
    }
}
if ($found.CMake) {
    $ctest = Join-Path (Split-Path $found.CMake) 'ctest.exe'
    $versionLine = & $found.CMake --version | Select-Object -First 1
    if ($LASTEXITCODE -ne 0 -or $versionLine -notmatch 'cmake version (\d+\.\d+)' -or
        [version]$Matches[1] -lt [version]'3.21' -or -not (Test-Path -LiteralPath $ctest)) {
        $found.CMake = $null
    }
}
if ((@($found.Values | Where-Object { -not $_ }).Count -gt 0) -and -not (Find-OptionalTool 'tar.exe')) {
    throw 'Windows tar.exe is required to bootstrap missing tools'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'vswhere not found' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'MSVC x64 tools not found' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$redist = Get-ChildItem -Path "$vs/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT" -Directory -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $redist) { throw 'MSVC x64 redistributable DLLs not found' }
foreach ($name in @('PATH', 'INCLUDE', 'LIB', 'LIBPATH')) {
    $value = & cmd.exe /d /v:on /c "call `"$vcvars`" >nul && echo !$name!"
    if ($LASTEXITCODE -ne 0 -or -not $value) { throw "MSVC environment setup failed for $name" }
    [Environment]::SetEnvironmentVariable($name, ($value -join "`n"), 'Process')
}
New-Item -ItemType Directory -Force -Path $ci, $build, $results | Out-Null
Install-CiTools "$ci/tools" $found
$sevenZip = $found.SevenZip
Install-Mpv 'x86_64-v3' "$build/mpv" $sevenZip $ci $mpvVersion '49533E80D681672E42AB38FCCFDB9DB0CC9FC423F86D34F5F9634EBA71109912'
Install-Mpv 'x86_64' "$build/mpv-fallback" $sevenZip $ci $mpvVersion 'D7F5621F42B6E161FE032DF914A10BDECE28A89DF3E57C6C3E16A5A250929B5E'
Run-Native $python @('-m', 'venv', "$ci/venv")
$venvPython = Join-Path $ci 'venv/Scripts/python.exe'
Run-Native $venvPython @('-m', 'pip', 'install', 'aqtinstall==3.3.0')
Run-Native $venvPython @('-m', 'aqt', 'install-qt', 'windows', 'desktop', $qtVersion, 'win64_msvc2022_64', '-m', 'qtwebengine', 'qtwebchannel', 'qtpositioning', '-O', "$ci/qt")
$exports = & dumpbin.exe /exports "$build/mpv/libmpv-2.dll"
if ($LASTEXITCODE -ne 0) { throw 'dumpbin failed' }
$names = @($exports | ForEach-Object { if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(mpv_\w+)\s*$') { $Matches[1] } })
if ($names.Count -eq 0) { throw 'No mpv exports found' }
@('LIBRARY libmpv-2.dll', 'EXPORTS') + $names | Set-Content -LiteralPath "$build/mpv/mpv.def" -Encoding ASCII
Run-Native 'lib.exe' @("/def:$build/mpv/mpv.def", "/out:$build/mpv/libmpv-2.dll.lib", '/MACHINE:X64')
New-Item -ItemType Directory -Force -Path "$deps/vcruntime" | Out-Null
Copy-Item -Path "$($redist.FullName)/*.dll" -Destination "$deps/vcruntime"
Invoke-WebRequest -UseBasicParsing 'https://aka.ms/vs/17/release/vc_redist.x64.exe' -OutFile "$deps/vc_redist.x64.exe"
$qt = "$ci/qt/$qtVersion/msvc2022_64"
$env:PATH = "$qt/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$qt/plugins"
Run-Native 'cmake.exe' @('-S', $root, '-B', $build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo', "-DCMAKE_INSTALL_PREFIX=$build/output", "-DQTROOT=$qt", "-DMPV_INCLUDE_DIR=$build/mpv/include", "-DMPV_LIBRARY=$build/mpv/libmpv-2.dll.lib", '-DCHECK_FOR_UPDATES=ON', '-DUSE_STATIC_MPVQT=ON')
Run-Native 'cmake.exe' @('--build', $build, '--parallel', '4')
Run-Native 'ctest.exe' @('--test-dir', $build, '--output-on-failure', '--timeout', '60', '--output-junit', "$results/ctest.xml")
Run-Native $venvPython @('tests/windows_mpv_loader_integration.py')
Run-Native $venvPython @('tests/windows_packaging_runtime_test.py')
$env:ISCC = $found.Inno
$env:WINDOWS_VCRUNTIME_DIR = "$deps/vcruntime"
$env:WINDOWS_VCREDIST_EXE = "$deps/vc_redist.x64.exe"
Run-Native $venvPython @('tests/windows_installer_compile_test.py')
Run-Native $found.Node @('--test', 'tests/test_inputPlugin.js')
Run-Native $found.Node @('tests/windows_hidden_desktop_fixture.mjs')
Run-Native 'cmake.exe' @('--build', $build, '--target', 'windows_all', '--parallel', '4')
$artifacts = @(Get-ChildItem -Path "$build/JellyfinDesktop-*.exe", "$build/JellyfinDesktop-*.zip")
if (@($artifacts | Where-Object Extension -eq '.exe').Count -ne 1 -or @($artifacts | Where-Object Extension -eq '.zip').Count -ne 1) { throw 'Expected exactly one installer and one portable ZIP' }
$artifacts | Get-FileHash -Algorithm SHA256 | ForEach-Object { "$($_.Hash)  $($_.Path | Split-Path -Leaf)" } | Set-Content "$results/SHA256SUMS.txt"
@("commit=$env:CI_COMMIT_SHA", "pipeline=$env:CI_PIPELINE_ID", "job=$env:CI_JOB_ID", "qt=$qtVersion", "mpv=$mpvVersion") | Set-Content "$results/provenance.txt"
